"""The device's hand-written runtime helpers: divide.s's ___udivsi3, ___umodsi3,
___divsi3 and ___modsi3, and memory.cpp's device memset and memcpy.

Nothing on the host executes Blackfin code (the pinned toolchain ships no
simulator), so these run the helpers' own assembly text in an instruction model
(tests/blackfin_model.py) and compare every result with C's. The model is
independent of the helpers but is itself a reading of the instruction set; the
helpers were also compared, old against new, on a Blackfin simulator outside this
repository, which these tests do not repeat. With the pinned toolchain present,
both files must also assemble and define their symbols.

Every call starts from a fresh model with every register poisoned, and returns
only if the callee-saved registers of GCC's Blackfin convention (R4-R7, P3-P5,
FP, SP) are unchanged; see blackfin_model.Machine.call. The ABI tests below show
the check catches a helper that breaks that convention.
"""
import os
import random
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from blackfin_model import MASK, Machine, signed
from test_divide import blackfin_compiler
from tooling.definitions import ROOT
from tooling.native import CXX_FLAGS

DIVIDE = (ROOT / 'sdk/platform/blackfin/divide.s').read_text(encoding='utf-8')
MEMORY_SOURCE = (ROOT / 'sdk/platform/blackfin/memory.cpp').read_text(encoding='utf-8')
MEMORY = MEMORY_SOURCE[MEMORY_SOURCE.index('asm(R"(') + 7:MEMORY_SOURCE.index(')");')]


def c_div(a, b):
    """C's truncating signed quotient and remainder."""
    q = abs(a) // abs(b)
    q = q if (a < 0) == (b < 0) else -q
    return q, a - q * b


HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


def run(command):
    """Runs a toolchain step with no console window; output goes into the failure message."""
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True, timeout=120, **HIDDEN)
    assert result.returncode == 0, ' '.join(map(str, command)) + '\n' + result.stdout + result.stderr
    return result.stdout


class Divide(unittest.TestCase):
    def setUp(self):
        self.machine = Machine(DIVIDE)

    def unsigned(self, n, d):
        m = self.machine.call('___udivsi3', R0=n, R1=d)
        return m.get('R0'), m.get('R3')

    def test_unsigned_quotient_and_remainder(self):
        rng = random.Random(1)
        cases = [(n, d) for n in range(0, 300) for d in range(1, 40)]
        for d in [1, 2, 3, 7, 10, 24, 255, 0x4000, 0x7ffe, 0x7fff, 0x8000, 0x8001, 0xffff, 0x10000,
                  0x12345, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff] + [rng.randrange(1, 0x8000) for _ in range(60)]:
            for n in {0, 1, d - 1, d, d + 1, 2 * d - 1, (d << 16) - 1, d << 16, (d << 16) + 1, 0xffff, 0x10000,
                      d * 0xffff, 0x7fffffff, 0x80000000, 0xffffffff, *(rng.randrange(1 << 32) for _ in range(12))}:
                cases.append((n & MASK, d & MASK))
        for n, d in cases:
            with self.subTest(n=n, d=d):
                self.assertEqual(self.unsigned(n, d), (n // d, n % d))
        # Division by zero: quotient all ones, remainder the dividend (C leaves it undefined).
        self.assertEqual(self.unsigned(1234, 0), (MASK, 1234))

    def test_signed_and_modulo_wrappers(self):
        values = [0, 1, -1, 2, -2, 7, -7, 24, -24, 0x7fff, -0x7fff, 0x8000, -0x8000, 0x12345678, -0x12345678,
                  0x7fffffff, -0x7fffffff, -0x80000000]
        for a in values:
            for b in [v for v in values if v]:
                with self.subTest(a=a, b=b):
                    if a == -0x80000000 and b == -1:
                        q, r = -0x80000000, 0  # overflows in C; the helpers wrap
                    else:
                        q, r = c_div(a, b)
                    self.assertEqual(signed(self.machine.call('___divsi3', R0=a, R1=b).get('R0')), q)
                    self.assertEqual(signed(self.machine.call('___modsi3', R0=a, R1=b).get('R0')), r)
                    if a >= 0 and b > 0:
                        self.assertEqual(self.machine.call('___umodsi3', R0=a, R1=b).get('R0'), a % b)


class Memory(unittest.TestCase):
    def setUp(self):
        self.machine = Machine(MEMORY, memory_base=0x1000, memory_size=0x200)

    def fill(self):
        self.machine.memory[:] = bytes((i * 37 + 11) & 0xff for i in range(len(self.machine.memory)))
        return bytearray(self.machine.memory)

    def test_memset_writes_exactly_the_bytes_the_byte_loop_writes(self):
        for start in range(8):
            for length in list(range(0, 41)) + [63, 64, 65, 100, 131]:
                for value in (0, 0xa5, 0x1ff):
                    with self.subTest(start=start, length=length, value=value):
                        expected = self.fill()
                        expected[0x40 + start:0x40 + start + length] = bytes([value & 0xff]) * length
                        m = self.machine.call('_memset', R0=0x1040 + start, R1=value, R2=length)
                        self.assertEqual(m.get('R0'), 0x1040 + start)
                        self.assertEqual(m.memory, expected)

    def test_memcpy_matches_a_forward_byte_copy_at_every_alignment_and_overlap(self):
        lengths = list(range(0, 22)) + [31, 32, 33, 63, 64, 65, 67]
        for destination in range(0x40, 0x48):
            for source in list(range(0x40, 0x48)) + [0x80, 0x81, 0x82, 0x83, 0x20, 0x21]:
                for length in lengths:
                    with self.subTest(destination=destination, source=source, length=length):
                        expected = self.fill()
                        for i in range(length):  # the byte loop's result, overlap included
                            expected[destination + i] = expected[source + i]
                        m = self.machine.call('_memcpy', R0=0x1000 + destination, R1=0x1000 + source, R2=length)
                        self.assertEqual(m.get('R0'), 0x1000 + destination)
                        self.assertEqual(m.memory, expected)


class Abi(unittest.TestCase):
    """The register check in Machine.call rejects a helper that clobbers a callee-saved
    register, even when its results are still right."""

    def mutated(self, text, old, new):
        self.assertEqual(text.count(old), 1, old)
        return text.replace(old, new)

    def test_divide_with_its_loop_count_in_p3_is_rejected(self):
        text = self.mutated(DIVIDE, 'P0 = R3;', 'P3 = R3;')
        text = self.mutated(text, 'LC0 = P0', 'LC0 = P3')
        # A divisor of 32768 or more takes the shift/subtract loop that counts in P0.
        with self.assertRaisesRegex(AssertionError, 'callee-saved P3 changed'):
            Machine(text).call('___udivsi3', R0=0x12345678, R1=0x10001)
        self.assertEqual(Machine(DIVIDE).call('___udivsi3', R0=0x12345678, R1=0x10001).get('R0'), 0x1234)

    def test_memset_that_writes_r4_is_rejected(self):
        text = self.mutated(MEMORY, '_memset:\n    P0 = R0;', '_memset:\n    P0 = R0;\n    R4 = R2;')
        with self.assertRaisesRegex(AssertionError, 'callee-saved R4 changed'):
            Machine(text, 0x1000, 0x200).call('_memset', R0=0x1040, R1=0, R2=40)
        Machine(MEMORY, 0x1000, 0x200).call('_memset', R0=0x1040, R1=0, R2=40)

    def test_poisoned_registers_reach_the_helper(self):
        # A helper that reads a register it was not given sees the poison, not zero.
        m = Machine('_f:\n    R0 = R5;\n    RTS;\n')
        self.assertEqual(m.call('_f').get('R0'), 0xa5a50005)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_compiled_code_saves_r4_to_r7_and_p3_to_p5_but_not_the_loop_counters(self):
        # The convention the check uses, read off the pinned compiler: a function that
        # needs many registers saves R4-R7/P3-P5 in its prologue, and a hardware loop
        # sets LC0/LC1 without saving them first.
        source = '''
            extern void g(int *);
            int many(int *a, int *b, int *c, int *d, int n) {
                int s = 0, t = 0, u = 0, v = 0, w = 0, x = 0;
                for (int i = 0; i < n; ++i) { s += a[i]; t += b[i]; u += c[i]; v += d[i]; w ^= a[i] * b[i]; x |= c[i] - d[i]; }
                g(a); return s + t + u + v + w + x;
            }'''
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'abi.cpp').write_text(source, encoding='utf-8')
            run([blackfin_compiler(), *CXX_FLAGS, '-S', folder / 'abi.cpp', '-o', folder / 'abi.s'])
            listing = (folder / 'abi.s').read_text(encoding='utf-8')
        self.assertRegex(listing, r'(?i)\[--SP\] = \( ?R7:\d, P5:\d ?\)')
        self.assertRegex(listing, r'(?i)LSETUP .* LC[01] =')
        self.assertNotRegex(listing, r'(?i)\[--SP\] = L[CTB][01]')


class Assembly(unittest.TestCase):
    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_both_files_build_with_the_pinned_toolchain(self):
        compiler = blackfin_compiler()
        assembler = compiler.with_name(compiler.name.replace('g++', 'as'))
        objdump = compiler.with_name(compiler.name.replace('g++', 'objdump'))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            run([compiler, *CXX_FLAGS, '-S', ROOT / 'sdk/platform/blackfin/memory.cpp', '-o', folder / 'memory.s'])
            for name, source in (('memory', folder / 'memory.s'), ('divide', ROOT / 'sdk/platform/blackfin/divide.s')):
                run([assembler, '-mcpu=bf707', source, '-o', folder / (name + '.o')])
            symbols = set()
            for name in ('memory', 'divide'):
                table = run([objdump, '-t', folder / (name + '.o')])
                symbols |= set(re.findall(r'\sg\s+F \.text\s+[0-9a-f]+ (\S+)', table))
        self.assertEqual(symbols, {'_memset', '_memcpy', '___udivsi3', '___umodsi3', '___divsi3', '___modsi3'})


if __name__ == '__main__':
    unittest.main()
