"""Off unless asked, by code: a build that selects no CPU capability links the same module, byte for byte, as the
same checkout with the CPU capabilities taken out of it (tests/off_path_code.py: what the copy leaves out, the
collections, the record). Both trees are built here and nothing is stored, so an SDK change that does not touch
the capabilities cannot fail it. By hand: python tests/off_path_code.py"""
from pathlib import Path
import tempfile
import unittest

from off_path_code import compare, cpu_capabilities, object_record, toolchain, without_capabilities
from tooling import native
from tooling.pipeline import ROOT, document


class WithoutCapabilities(unittest.TestCase):
    def test_the_copy_has_none_of_them_and_the_rest_unchanged(self):
        names = set(cpu_capabilities(ROOT))
        extension = ROOT / 'extensions/cpu/extension.json'
        if extension.is_file():
            # idle is deprecated and out of cpu; its code is still the SDK's, so the copy removes it too.
            self.assertEqual([c for c in cpu_capabilities(ROOT) if c != 'idle'], document(extension)['capabilities'])
            self.assertIn('idle', names)
        with tempfile.TemporaryDirectory() as work:
            bare = without_capabilities(ROOT, Path(work) / 'bare')
            registry = document(bare / 'profiles/extensions.json')['capabilities']
            profile = document(bare / 'profiles/opxy-1.2.1.json')
            self.assertFalse(names & set(registry))
            self.assertFalse(names & set(profile['services']))
            self.assertFalse([p for p in profile['patches'] if p.get('service') in names])
            self.assertEqual(len(profile['patches']),
                             len([p for p in document(ROOT / 'profiles/opxy-1.2.1.json')['patches']
                                  if p.get('service') not in names]))
            for unit, service in native.UNIT_SERVICES.items():
                if service in names:
                    self.assertFalse([f for f in (bare / 'sdk/platform').rglob(unit + '.*') if f.suffix in ('.cpp', '.s')])
            for chosen in getattr(native, 'UNIT_SOURCES', {}).values():
                for source in chosen.values():
                    self.assertFalse((bare / source).exists())
            self.assertFalse(any('@IDLE_' in t.read_text(encoding='utf-8') for t in (bare / 'sdk/platform').rglob('*.in')))
            # Everything else is the checkout's, byte for byte.
            for rel in ('sdk/platform/blackfin/divide.s', 'sdk/platform/blackfin/memory.cpp', 'tooling/adapters.py',
                        'sdk/platform/opxy-1.2.1/drum_send.cpp'):
                self.assertEqual((bare / rel).read_bytes(), (ROOT / rel).read_bytes(), rel)


class OffPathCode(unittest.TestCase):
    @unittest.skipUnless(toolchain(), 'needs the pinned Blackfin tools (set OPXY_TEST_COMPILER) to compile and link')
    def test_nothing_asked_links_what_the_checkout_without_the_capabilities_links(self):
        with tempfile.TemporaryDirectory() as work:
            found = compare(ROOT, toolchain(), work)
        self.assertFalse(found, 'a build that asks for no capability differs from the checkout without them '
                                '(python tests/off_path_code.py lists it):\n' +
                                '\n'.join(f'{key}: {line}' for key, line in found))

    @unittest.skipUnless(toolchain(), 'needs the pinned Blackfin tools (set OPXY_TEST_COMPILER)')
    def test_the_record_sees_bytes_no_symbol_covers(self):
        """A same-length change of a string literal, or code under a bare label, changes the record although no
        function's own bytes change (the per-symbol record alone missed both)."""
        name = 'extern "C" const char* name(int i) { static const char* const n[] = {"1/32 %s", "1/16"}; return n[i & 1]; }\n'
        sources = {'base': name % 'dotted', 'text': name % 'dottxd',
                   'code': name % 'dotted' + 'asm(".text\\n.global loose\\nloose: NOP; NOP; RTS;\\n");\n'}
        cxx = next((toolchain() / 'bin').glob('bfin-elf-g++*'))
        assembler = next((toolchain() / 'bin').glob('bfin-elf-as*'))
        found = {}
        with tempfile.TemporaryDirectory() as work:
            for key, text in sources.items():
                source, out = Path(work) / f'{key}.cpp', Path(work) / f'{key}.o'
                source.write_text(text, encoding='utf-8')
                native.compile_cxx(cxx, assembler, source, out)
                found[key] = object_record(out, {'_name'})
        self.assertEqual(found['base']['_name'], found['text']['_name'], 'the function itself is unchanged')
        self.assertNotEqual(found['base'], found['text'])
        self.assertNotEqual(found['base'], found['code'])


if __name__ == '__main__':
    unittest.main()
