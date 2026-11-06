"""A machine's own Blackfin routines (native/<name>.s, opxy/native.hpp): where they may
live, the manifest and lint that describe them, the C reference elsewhere, the device
object's shape, and a firmware compile that links each into its own machine.

tests/fixtures/native holds the example: a gain loop and its C reference, written
for probesources. Whether a routine computes what its reference does is the
machine's to test (a simulator comparison); these checks are static.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.machines import NativeUnit, machine_at
from tooling.definitions import ROOT
from machine_fixtures import FOLDER, fixture
from test_divide import blackfin_compiler
from test_sample_services import HOST_CXX

EXAMPLE = Path(__file__).resolve().parent / 'fixtures/native'
CALL = '    probesources::apply_gain(output, opxy::block_frames, 16384);\n'


def with_native(directory, name='probesources', files=None, call=True):
    """A copy of a fixture machine carrying the example unit (renamed to its prefix)."""
    folder = Path(directory) / name
    shutil.copytree(FOLDER / name, folder)
    (folder / 'native').mkdir()
    for path in EXAMPLE.iterdir():
        (folder / 'native' / path.name).write_text(path.read_text().replace('probesources', name), newline='\n')
    for relative, text in (files or {}).items():
        target = folder / relative
        if text is None:
            target.unlink()
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text, newline='\n')
    if call:
        engine = folder / 'engine.cpp'
        source = engine.read_text().replace('#include "core.h"\n', '#include "core.h"\n#include "native/gain.hpp"\n', 1)
        end = source.rindex('\n    }\n}') + len('\n    }\n')
        engine.write_text(source[:end] + CALL.replace('probesources', name) + source[end:], newline='\n')
    return folder


def mutated(old, new):
    text = (EXAMPLE / 'gain.s').read_text()
    assert old in text, old
    return {'native/gain.s': text.replace(old, new, 1)}


def manifest(**changes):
    record = json.loads((EXAMPLE / 'gain.json').read_text())
    record.update(changes)
    return {'native/gain.json': json.dumps({k: v for k, v in record.items() if v is not None})}


class NativeUnits(unittest.TestCase):
    def test_a_unit_links_into_its_own_machine(self):
        with tempfile.TemporaryDirectory() as directory:
            m = machine_at(with_native(directory))
            self.assertEqual([(u.name, u.symbol, u.stack_bytes, u.state_bytes) for u in m.native],
                             [('gain', 'probesources_native_gain', 0, 0)])
            other = fixture('probesynth')
            units = [(name, owner.id) for name, owner, step in native.plan([other, m]) if step.startswith('native:')]
            self.assertEqual(units, [('probesources/native_gain.o', 'probesources')])
            self.assertEqual(native.object_owners([other, m])['probesources/native_gain.o'], 'probesources')

    def test_machines_each_carry_their_own_units(self):
        # Prefixed symbols: two machines may hold a routine of the same name.
        with tempfile.TemporaryDirectory() as directory:
            a, b = machine_at(with_native(directory)), machine_at(with_native(directory, 'probesynth', call=False))
            units = [(name, step) for name, _m, step in native.plan([a, b]) if step.startswith('native:')]
            self.assertEqual(units, [('probesources/native_gain.o', 'native:gain'), ('probesynth/native_gain.o', 'native:gain')])
            self.assertEqual([u.symbol for u in b.native], ['probesynth_native_gain'])

    def test_layout_and_manifest_are_checked(self):
        cases = {
            'assembly outside native/': ({'lib/fast.s': '.section .text\n'}, 'assembly lives in native/<name>.s'),
            'preprocessed assembly': ({'native/other.S': '.section .text\n'}, 'assembly lives in native/<name>.s'),
            'no manifest': ({'native/gain.json': None}, 'has its manifest'),
            'manifest alone': ({'native/spare.json': '{}'}, 'has its manifest'),
            'missing range': (manifest(range=None), 'symbol, stackBytes and range'),
            'unknown field': (manifest(cycles=10), 'symbol, stackBytes and range'),
            'foreign prefix': (manifest(symbol='probesynth_native_gain'), 'starts with probesources_native_'),
            'odd stack': (manifest(stackBytes=6), 'multiple of 4'),
            'huge stack': (manifest(stackBytes=1 << 16), 'stackBytes is a multiple of 4'),
        }
        for label, (files, message) in cases.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as directory:
                with self.assertRaisesRegex(ValueError, message):
                    machine_at(with_native(directory, files=files))

    def test_assembly_lint(self):
        cases = {
            'second export': mutated('.global _probesources_native_gain', '.global _probesources_native_gain, _helper'),
            'unprefixed export': mutated('.global _probesources_native_gain', '.global _gain'),
            'writable data': mutated('.section .text', '.section .data'),
            'section flags': mutated('.section .text', '.section .text,"awx"'),
            'other section': mutated('.section .text', '.section .init_array'),
            'include': mutated('.section .text', '.include "other.s"\n.section .text'),
            'weak': mutated('.section .text', '.weak _probesources_native_gain\n.section .text'),
            'common': mutated('.section .text', '.comm _buffer, 64\n.section .text'),
            'macro arguments': mutated('.section .text', '.macro twice x\n.endm\n.section .text'),
            'macro substitution': mutated('.section .text', '.macro twice\n    R0 = \@;\n.endm\n.section .text'),
            'repeat over a list': mutated('.section .text', '.irp r, R0\n.endr\n.section .text'),
            'privileged macro': mutated('.section .text', '.macro stop\n    IDLE;\n.endm\n.section .text'),
            'raise': mutated('    RTS;', '    RAISE 2;\n    RTS;'),
            'idle': mutated('    RTS;', '    IDLE;\n    RTS;'),
            'interrupts': mutated('    RTS;', '    CLI R3;\n    RTS;'),
            'system register': mutated('    RTS;', '    SYSCFG = R0;\n    RTS;'),
            'nested interrupt': mutated('    RTS;', '    [--SP] = RETI;\n    RTS;'),
            'cache': mutated('    RTS;', '    FLUSHINV [P0];\n    RTS;'),
            'cycle counter': mutated('    RTS;', '    CYCLES = R0;\n    RTS;'),
            'indirect call': mutated('    RTS;', '    CALL (P0);\n    RTS;'),
            'stack from register': mutated('    RTS;', '    SP = P0;\n    RTS;'),
            'stack by register': mutated('    RTS;', '    SP += P1;\n    RTS;'),
            'stack post-decrement': mutated('    RTS;', '    R0 = [SP--];\n    RTS;'),
            'memory-mapped register': mutated('    RTS;', '    P1.H = 0xFFC0;\n    RTS;'),
            'memory-mapped, negative': mutated('    RTS;', '    I0.H = -32;\n    RTS;'),
            'memory-mapped, high part': mutated('    RTS;', '    P1.H = HI(0xFFC01000);\n    RTS;'),
            'stack by halves': mutated('    RTS;', '    SP.L = 0;\n    RTS;'),
            'circular buffer left on': mutated('    RTS;', '    L1 = R0;\n    RTS;'),
            'circular buffer by halves': mutated('    RTS;', '    L3.L = 24;\n    RTS;'),
        }
        messages = {'second export': 'exports exactly', 'unprefixed export': 'exports exactly',
                    'writable data': 'stateBytes', 'section flags': 'without flags', 'other section': 'sections',
                    'include': 'directive', 'weak': 'directive', 'common': 'directive', 'macro arguments': 'take no arguments',
                    'macro substitution': 'substitution', 'repeat over a list': 'directive',
                    'cycle counter': 'cycle counters', 'indirect call': 'indirect CALL',
                    'memory-mapped register': 'memory-mapped', 'memory-mapped, negative': 'memory-mapped',
                    'memory-mapped, high part': 'memory-mapped',
                    'circular buffer left on': 'L1 must be 0', 'circular buffer by halves': 'L3 must be 0'}
        for label, files in cases.items():
            message = messages.get(label, 'change SP only' if label.startswith('stack') else 'supervisor or system')
            with self.subTest(label), tempfile.TemporaryDirectory() as directory:
                with self.assertRaisesRegex(ValueError, message):
                    machine_at(with_native(directory, files=files))

    def test_lint_allows_what_routines_need(self):
        allowed = {
            'stack frame': mutated('    CC = R1 == 0;', '    [--SP] = (R7:4, P5:3);\n    LINK 16;\n    SP += -8;\n'
                                   '    SP += 8;\n    UNLINK;\n    (R7:4, P5:3) = [SP++];\n    CC = R1 == 0;'),
            'circular buffer reset': mutated('    RTS;', '    L2 = 24;\n    L2 = 0;\n    RTS;'),
            'computed jump': mutated('    RTS;', '    JUMP (P1);\n    RTS;'),
            'plain macro': mutated('.section .text', '.macro twice\n    R0 = R0 + R0;\n.endm\n.section .text'),
            'constants': mutated('.section .text', '.section .rodata\n.align 4\ntable: .long 1, 2, -3\n.section .text'),
            'explanatory comments': mutated('    RTS;', '    /* never RAISE, IDLE or SP = P0 here */\n'
                                            '    # nor CALL (P0)\n    RTS;'),
            'data address halves': mutated('    P0 = R0;', '    P0 = R0;\n    P2.H = 0x1234;\n    R1.H = -64;\n'
                                           '    P2.H = HI(0x11800000);'),
        }
        for label, files in allowed.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as directory:
                machine_at(with_native(directory, files=files))
        # Writable state, when the manifest declares it.
        with tempfile.TemporaryDirectory() as directory:
            files = {**mutated('.section .text', '.section .data\n.align 4\ncache: .long 0\n.section .text'),
                     **manifest(stateBytes=4)}
            self.assertEqual([u.state_bytes for u in machine_at(with_native(directory, files=files)).native], [4])

    def test_device_object_shape(self):
        listing = '''
k.o:     file format elf32-bfin

Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000020  00000000  00000000  00000034  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .data         {data}  00000000  00000000  00000054  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  2 .bss          00000000  00000000  00000000  00000058  2**0
                  ALLOC
SYMBOL TABLE:
00000000 l    d  .text	00000000 .text
00000010 l       .text	00000000 .Ldone
00000000 g     F .text	00000020 _probesources_native_gain
{extra}
'''
        unit = NativeUnit('gain', None, 'probesources_native_gain', 0, 0, 'any', '')
        native.check_native_object(listing.format(data='00000000', extra=''), unit, 'gain')
        native.check_native_object(listing.format(data='00000000', extra='00000000         *UND*\t00000000 _memcpy'),
                                   unit, 'gain')
        bad = {'state': (listing.format(data='00000004', extra=''), 'unexpected sections .data'),
               'export': (listing.format(data='00000000', extra='00000000 g     O .text\t00000004 _probesources_native_x'),
                          'exports one function'),
               'weak': (listing.format(data='00000000', extra='00000000  w    F .text\t00000004 _other'), 'exports one function')}
        for label, (text, message) in bad.items():
            with self.subTest(label), self.assertRaisesRegex(ValueError, message):
                native.check_native_object(text, unit, 'gain')
        stateful = NativeUnit('gain', None, 'probesources_native_gain', 0, 4, 'any', '')
        native.check_native_object(listing.format(data='00000004', extra=''), stateful, 'gain')
        with self.assertRaisesRegex(ValueError, 'more than its stateBytes 2'):
            native.check_native_object(listing.format(data='00000004', extra=''), stateful.__class__(
                'gain', None, 'probesources_native_gain', 0, 2, 'any', ''), 'gain')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_reference_runs_off_the_device(self):
        program = r'''
#include "gain.hpp"
int main() {
    opxy::i32 a[4] = {0x7fffffff, -0x7fffffff - 1, 0x12345678, -5}, b[4] = {0x7fffffff, -0x7fffffff - 1, 0x12345678, -5};
    probesources::apply_gain(a, 4, 32767);
    for (int i = 0; i < 4; ++i) if (a[i] != (b[i] >> 16) * 32767 * 2) return 1;
    return 0;
}
'''
        mismatch = '''
#include <opxy/native.hpp>
extern "C" void m_native_f(int* x, unsigned n);
inline void f_reference(int* x, int n) { x[0] = n; }
void g(int* x) { OPXY_NATIVE(m_native_f, f_reference)(x, 1); }
'''
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / 'check.cpp', Path(directory) / 'check'
            source.write_text(program)
            # No device symbol exists here, and the host build links without one.
            subprocess.run([HOST_CXX, '-std=c++11', '-O0', '-Wall', '-Werror', '-I', str(ROOT / 'sdk/include'),
                            '-I', str(EXAMPLE), str(source), '-o', str(binary)], check=True)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 0)
            source.write_text(mismatch)
            built = subprocess.run([HOST_CXX, '-std=c++11', '-fsyntax-only', '-I', str(ROOT / 'sdk/include'), str(source)],
                                   capture_output=True, text=True)
            self.assertNotEqual(built.returncode, 0, 'a reference of another type must not compile')
            self.assertIn('same_signature', built.stderr)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_firmware_compile_links_the_routine(self):
        home = blackfin_compiler().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            folder = with_native(directory)
            out = Path(directory) / 'module'
            subprocess.run([os.sys.executable, str(ROOT / 'sdk.py'), 'compile', '--toolchain', str(home / 'bin'),
                            '--compiler', str(home), '--profiling', 'off', '--machine', str(folder), '--out', str(out)],
                           check=True, cwd=ROOT, capture_output=True, timeout=600)
            report = json.loads((out / 'compile-report.json').read_text())
            self.assertEqual(report['objects']['probesources/native_gain.o'], 'probesources')
            disassembly = (out / 'disassembly.txt').read_text()
            self.assertIn('<_probesources_native_gain>:', disassembly)
            self.assertRegex(disassembly, r'(CALL|JUMP\.[LSX]) 0x[0-9a-f]+ <_probesources_native_gain>')
            # A declared frame counts in the audio stack bound even when larger than measured.
            (folder / 'native/gain.json').write_text(json.dumps(dict(
                json.loads((folder / 'native/gain.json').read_text()), stackBytes=1024)))
            bigger = Path(directory) / 'bigger'
            subprocess.run([os.sys.executable, str(ROOT / 'sdk.py'), 'compile', '--toolchain', str(home / 'bin'),
                            '--compiler', str(home), '--profiling', 'off', '--machine', str(folder), '--out', str(bigger)],
                           check=True, cwd=ROOT, capture_output=True, timeout=600)
            stack = json.loads((bigger / 'compile-report.json').read_text())['audio_stack_bytes']['probesources']
            self.assertGreaterEqual(stack, 1024)


if __name__ == '__main__':
    unittest.main()
