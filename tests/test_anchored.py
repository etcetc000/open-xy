"""opxy::q31::anchored: the definition's evenly spaced anchors, exact at stock's quarter captions."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.definitions import ROOT
from test_sample_services import HOST_CXX

PROGRAM = r'''
#include <opxy/q31.hpp>
#include <cstdio>
using opxy::i32;
using opxy::i64;
using opxy::u32;
int main() {
    // Five anchors land on the words stock captions as 00, 25, 50, 75 and 99.
    const i32 frames[5] = {220, 441, 662, 882, 1102};
    const i32 quarters[5] = {0, 0x20000000, 0x40000000, 0x60000000, 0x7fffffff};
    for (u32 i = 0; i < 5; ++i)
        if (opxy::q31::anchored(quarters[i], frames, 5) != frames[i]) return 1 + i;
    if (opxy::q31::anchored(-1, frames, 5) != 220 || opxy::q31::anchored(-0x7fffffff - 1, frames, 5) != 220) return 6;
    if (opxy::q31::anchored(0x10000000, frames, 5) != 330) return 7;  // halfway to 441, rounded down
    if (opxy::q31::anchored(0x1fffffff, frames, 5) != 440) return 8;
    // Falling, signed and full-range anchors stay exact; a stored Q15 full scale is the last.
    const i32 falling[3] = {100, -100, 0};
    if (opxy::q31::anchored(0x40000000, falling, 3) != -100 || opxy::q31::anchored(0x20000000, falling, 3) != 0) return 9;
    if (opxy::q31::anchored(0x60000000, falling, 3) != -50) return 10;
    const i32 wide[2] = {0, 0x7fffffff};
    if (opxy::q31::anchored(0x40000000, wide, 2) != 0x3fffffff || opxy::q31::anchored(0x7fff0000, wide, 2) != 0x7fffffff) return 11;
    // Every word maps between its neighbouring anchors.
    for (i64 w = 0; w <= 0x7fffffff; w += 0x00123457) {
        const i32 value = opxy::q31::anchored(static_cast<i32>(w), frames, 5);
        const u32 segment = static_cast<u32>(w * 4 >> 31);
        if (value < frames[segment] || value > frames[segment + 1]) { std::printf("%lld\n", static_cast<long long>(w)); return 12; }
    }
    return 0;
}
'''


class AnchoredTests(unittest.TestCase):
    def test_quarter_anchors_are_exact(self):
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        hidden = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory() as folder:
            source, binary = Path(folder) / 'anchored.cpp', Path(folder) / 'anchored.exe'
            source.write_text(PROGRAM, encoding='utf-8')
            subprocess.run([HOST_CXX, '-std=c++11', '-O1', '-Wall', '-Werror', '-I', str(ROOT / 'sdk/include'),
                            str(source), '-o', str(binary)], check=True, capture_output=True, timeout=120, **hidden)
            result = subprocess.run([str(binary)], capture_output=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout)
