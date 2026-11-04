"""Module files: the relocation list reproduces a second link, and the loader accepts the result."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from tooling import build_module
from tooling.pipeline import ROOT


class Packing(unittest.TestCase):
    def test_apply_writes_every_kind_at_the_new_base(self):
        image = bytes(16)
        relocs = [(build_module.ABS32 << 28 | 0, 8), (build_module.HI16 << 28 | 4, 12), (build_module.LO16 << 28 | 6, 12)]
        moved = build_module.apply(image, relocs, 0x01234560)
        self.assertEqual(struct.unpack_from('<I', moved, 0)[0], 0x01234568)
        self.assertEqual(struct.unpack_from('<HH', moved, 4), (0x0123, 0x456c))
        self.assertEqual(build_module.apply(image, [], 0x01234560), image)

    def test_trailer_is_last_and_covers_image_and_relocs(self):
        image = bytes(range(8))
        description = build_module.manifest('test', '1.0.0', 'module')
        data = build_module.pack(description, image, 4, 2, [(build_module.ABS32 << 28, 4)])
        fields = build_module.TRAILER.unpack(data[-48:])
        self.assertEqual(fields[-1], b'OPXYMOD\0')
        text, at = fields[0], fields[1]
        self.assertEqual((at % 4, at - text < 4), (0, True))
        self.assertEqual(fields[2:6], (8, 4, at + 8, 1))
        self.assertEqual(data[at:at + 8], image)
        self.assertEqual(fields[6], 2)
        self.assertEqual(fields[7], zlib.crc32(data[at + 8:at + 16], zlib.crc32(image)))
        self.assertEqual(fields[8:11], (build_module.OS_1_2_1, 0, 48))
        self.assertEqual(build_module.inspect(data)['manifest'], description)

    def test_the_manifest_says_what_the_module_is(self):
        self.assertEqual(build_module.manifest('karplus', '1.2.10', 'synth'),
                         dict(manifest=1, name='karplus', version='1.2.10', kind='synth', os='1.2.1',
                              api=build_module.api_version()))
        self.assertGreaterEqual(build_module.api_version(), 2)
        for name in ('', 'a b', 'a.b', 'x' * 16, 'ü'):
            with self.assertRaises(ValueError):
                build_module.manifest(name, '1.0.0', 'synth')
        for version in ('', '1', '1.0', '01.0.0', '1.0.0-beta', 'v1.0.0', '1.0.0.0', None):
            with self.assertRaises(ValueError):
                build_module.manifest('karplus', version, 'synth')

    def test_a_file_without_a_readable_manifest_is_refused(self):
        image = bytes(8)
        data = build_module.pack(build_module.manifest('test', '1.0.0', 'module'), image, 0, 0, [])
        fields = list(build_module.TRAILER.unpack(data[-48:]))
        for manifest_bytes in (0, fields[0] - 2):
            fields[0] = manifest_bytes
            with self.assertRaises(ValueError):
                build_module.inspect(data[:-48] + build_module.TRAILER.pack(*fields))
        fields[0] = fields[1] + 4  # into the image
        with self.assertRaises(ValueError):
            build_module.inspect(data[:-48] + build_module.TRAILER.pack(*fields))

    def test_unknown_relocation_types_are_refused(self):
        with self.assertRaises(ValueError):
            build_module.relocations([('.text', 0, 0x05, 0)], bytes(8), 0)   # R_BFIN_RIMM16
        self.assertEqual(build_module.relocations([('.text', 0, 0x0a, 0)], bytes(8), 0), [])   # PCREL24


class Probe(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('OPXY_TEST_TOOLCHAIN'), 'No pinned Blackfin tools fixture')
    def test_probe_module_builds_and_stages(self):
        tools = Path(os.environ['OPXY_TEST_TOOLCHAIN'])
        compiler = tools.parent
        if not (compiler / 'bin' / ('bfin-elf-g++' + ('.exe' if os.name == 'nt' else ''))).is_file():
            raise unittest.SkipTest('Compiler not beside the tools')
        host = os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not host: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'probe.opx'
            result = build_module.build(ROOT / 'tests/fixtures/modules/probe', out, '0.1.0', tools, compiler)
            self.assertGreater(result['relocs'], 0)
            self.assertEqual((result['manifest']['name'], result['manifest']['version'], result['manifest']['kind']),
                             ('probe', '0.1.0', 'module'))
            data = out.read_bytes()
            relocs = result['image_offset'] + result['image']
            kinds = {struct.unpack_from('<I', data, relocs + 8 * i)[0] >> 28 for i in range(result['relocs'])}
            self.assertEqual(kinds, {build_module.ABS32, build_module.HI16, build_module.LO16})
            runner = Path(temp) / ('loader.exe' if os.name == 'nt' else 'loader')
            subprocess.run([host, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform'),
                            str(ROOT / 'tests/fixtures/module_loader.cpp'),
                            str(ROOT / 'sdk/platform/module_loader.cpp'), '-o', str(runner)],
                           check=True, capture_output=True)
            subprocess.run([str(runner), str(out)], check=True)


if __name__ == '__main__': unittest.main()
