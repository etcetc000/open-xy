"""SDK kernels (opxy/kernels/*.hpp): linked only into the machine that includes one,
portable references that compile cleanly, and the device units' shape.

The device kernels were checked against these references sample for sample on
the BF707 functional simulator (random in-range calls, every control extreme and
the Macro Osc sweep).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.definitions import ROOT
from tooling.machines import machine_at
from machine_fixtures import FOLDER, fixture
from test_divide import blackfin_compiler
from test_sample_services import HOST_CXX

KERNELS = ('fold_pair',)

PROGRAM = r'''
#include <opxy/kernels/fold_pair.hpp>
using namespace opxy;
static u32 r = 1;
static u32 rnd() { r = r * 1664525u + 1013904223u; return r ^ (r >> 15); }
int main() {
    i16 a[24];
    // The fold pair: controls in range for the kernel, and its state advances.
    static u32 table[256];
    for (u32 i = 0; i < 256; ++i) table[i] = rnd();
    kernels::FoldPair f = {1, 2, 1u << 28, 1u << 27, 0xffffffffu, 5, 32767 / 24, 100, 32667, 32767, -32767, 0, 32767,
                           table, table, table};
    if (!kernels::fold_pair_in_range(f, 24) || kernels::fold_pair_in_range(f, 25)) return 8;
    f.triangle_delta = -32768;
    if (kernels::fold_pair_in_range(f, 24)) return 9;
    kernels::fold_pair(f, a, 24);
    return f.sine_increment == (1u << 28) - 24 && f.triangle_increment == (1u << 27) + 24 * 5 ? 0 : 10;
}
'''


class KernelTest(unittest.TestCase):
    def test_a_kernel_links_only_into_the_machine_including_it(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'probesources'
            shutil.copytree(FOLDER / 'probesources', folder)
            wave = folder / 'lib/wave.cpp'
            wave.write_text('#include <opxy/kernels/fold_pair.hpp>\n' + wave.read_text())
            user, other = machine_at(folder), fixture('probesynth')
            self.assertEqual(native.kernels(user), ['fold_pair'])
            self.assertEqual(native.kernels(other), [])
            units = native.plan([other, user])
            kernel_units = [(name, m.id, step) for name, m, step in units if step.startswith('kernel:')]
            self.assertEqual(kernel_units, [('probesources/kernel_fold_pair.o', 'probesources', 'kernel:fold_pair')])
            self.assertEqual(native.object_owners([other, user])['probesources/kernel_fold_pair.o'], 'probesources')
            # Kernel objects define fixed symbols: two machines may not both include one.
            twin = Path(directory) / 'probetwin'
            shutil.copytree(folder, twin)
            with self.assertRaisesRegex(ValueError, 'more than one machine'):
                native.plan([user, machine_at(twin)])

    def test_every_kernel_header_has_a_device_unit(self):
        headers = sorted(p.stem for p in (ROOT / 'sdk/include/opxy/kernels').glob('*.hpp'))
        units = sorted(p.stem for p in (ROOT / native.KERNELS).glob('*.s'))
        self.assertEqual(headers, list(KERNELS))
        self.assertEqual(units, list(KERNELS))

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_portable_references(self):
        with tempfile.TemporaryDirectory() as directory:
            source, program = Path(directory) / 'kernels.cpp', Path(directory) / 'kernels'
            source.write_text(PROGRAM)
            subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'sdk/include'),
                            str(source), '-o', str(program)], check=True)
            self.assertEqual(subprocess.run([str(program)]).returncode, 0)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_device_units_assemble_with_their_entry_points(self):
        tools = Path(blackfin_compiler()).parent
        assembler = next(p for p in (tools / 'bfin-elf-as.exe', tools / 'bfin-elf-as') if p.is_file())
        objdump = next(p for p in (tools / 'bfin-elf-objdump.exe', tools / 'bfin-elf-objdump') if p.is_file())
        with tempfile.TemporaryDirectory() as directory:
            for name in KERNELS:
                obj = Path(directory) / (name + '.o')
                subprocess.run([str(assembler), '-mcpu=bf707', '-o', str(obj), str(ROOT / native.KERNELS / (name + '.s'))],
                               check=True)
                symbols = subprocess.run([str(objdump), '-t', '-r', str(obj)], check=True, capture_output=True,
                                         text=True).stdout
                self.assertRegex(symbols, r'g\s+F \.text\s+[0-9a-f]+ _opxy_' + name)
                self.assertNotIn('R_BFIN_RIMM16', symbols)


if __name__ == '__main__':
    unittest.main()
