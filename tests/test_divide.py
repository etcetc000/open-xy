"""opxy::divide::quotient16: the portable quotient, and the device form inline."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.definitions import ROOT
from tooling.native import CXX_FLAGS
from test_sample_services import HOST_CXX

PROGRAM = r'''
#include <opxy/divide.hpp>
using opxy::u32;
int main() {
    u32 r = 7;
    for (u32 i = 0; i < 200000; ++i) {
        r = r * 1664525u + 1013904223u; const u32 d = (r >> 9) % 32767u + 1;
        r = r * 1664525u + 1013904223u; const u32 n = r % (d << 16);
        if (opxy::divide::quotient16(n, d) != n / d) return 1;
    }
    for (u32 d = 1; d < 0x8000; ++d) {
        const u32 top = (d << 16) - 1;
        const u32 ns[] = {0, 1, d - 1, d, d + 1, top, top - d, top - d + 1, d << 15, top >> 1};
        for (u32 n : ns) if (opxy::divide::quotient16(n, d) != n / d) return 2;
    }
    return opxy::divide::quotient16(0xffffu * 0x7fffu, 0x7fffu) == 0xffffu ? 0 : 3;
}
'''

DEVICE = r'''
#include <opxy/divide.hpp>
opxy::u32 position(opxy::u32 n, opxy::u32 d) { return opxy::divide::quotient16(n, d); }
'''


def blackfin_compiler():
    home = Path(os.environ.get('OPXY_TEST_COMPILER') or ROOT / 'local/toolchain')
    found = [p for p in (home / 'bin/bfin-elf-g++.exe', home / 'bin/bfin-elf-g++') if p.is_file()]
    return found[0] if found else None


class DivideTest(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_portable_quotient_is_exact_in_range(self):
        with tempfile.TemporaryDirectory() as directory:
            source, program = Path(directory) / 'divide.cpp', Path(directory) / 'divide'
            source.write_text(PROGRAM)
            subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I', str(ROOT / 'sdk/include'),
                            str(source), '-o', str(program)], check=True)
            self.assertEqual(subprocess.run([str(program)]).returncode, 0)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_device_quotient_is_inline_divide_steps(self):
        # Exactness of the device form was checked on the BF707 functional
        # simulator over every divisor; this pins its shape: no call, 17 steps.
        with tempfile.TemporaryDirectory() as directory:
            source, listing = Path(directory) / 'divide.cpp', Path(directory) / 'divide.s'
            source.write_text(DEVICE)
            subprocess.run([str(blackfin_compiler()), *CXX_FLAGS, '-I', str(ROOT / 'sdk/include'), '-S',
                            str(source), '-o', str(listing)], check=True)
            text = listing.read_text()
            self.assertEqual(text.count('DIVS ('), 1)
            self.assertEqual(text.count('DIVQ ('), 16)
            self.assertNotIn('call', text)


if __name__ == '__main__':
    unittest.main()
