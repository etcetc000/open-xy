"""opxy::q15 and opxy::q31's saturating helpers: the host forms against independent
references (tests/fixtures/fixed_point.cpp), and the device forms as compiled by the
pinned compiler: the expected instructions, no call, and no access to ASTAT or its
rounding mode. Headers that are included but not called add no code.

The device forms' results were checked once against the host forms on the BF707
instruction-set simulator: every 1.15 value for the unary helpers, the edge
cross-products, the ties and a million random pairs for the binary ones, Q31
edges at every count 0..31 for shift_right, and edges plus random words for the
saturates and q31::subtract; SDK 1.28's helpers the same way (every 1.15 value
for q15::half, edge and random pairs for the rest, every count -31..31 for
q31::shift). These tests pin the shape that was checked.

Each helper also compiles exactly as its instruction written out as inline
assembly that declares the arithmetic flags (ASTAT and CC) changed, so code that
moves from such assembly to the helper keeps its object code."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tooling.definitions import ROOT
from tooling.formats import require
from tooling.native import CXX_FLAGS
from test_divide import blackfin_compiler
from test_sample_services import HOST_CXX

HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
R = r'R[0-7]'
# Each helper's probe and the instructions its body must be, in order, as the pinned
# objdump spells them (a function's closing RTS aside). Where operand order matters the
# registers are named: the probe's a arrives in R0, b in R1, and the result leaves in R0.
PROBES = {
    'q15::saturate': ('opxy::i16 probe(opxy::i32 x) { return opxy::q15::saturate(x); }',
                      [rf'{R} = {R} << 0x10 \(S\);', rf'{R} >>>= 0x10;']),
    'q15::add': ('opxy::i16 probe(opxy::i16 a, opxy::i16 b) { return opxy::q15::add(a, b); }',
                 [rf'{R}\.L = {R}\.L \+ {R}\.L \(S\);']),
    'q15::subtract': ('opxy::i16 probe(opxy::i16 a, opxy::i16 b) { return opxy::q15::subtract(a, b); }',
                      [rf'{R}\.L = {R}\.L - {R}\.L \(S\);']),
    'q15::normalize': ('opxy::i16 probe(opxy::i16 a) { return opxy::q15::normalize(a); }',
                       [rf'{R}\.L = SIGNBITS {R}\.L;', rf'{R}\.L = ASHIFT {R}\.L BY {R}\.L \(S\);']),
    'q15::multiply': ('opxy::i16 probe(opxy::i16 a, opxy::i16 b) { return opxy::q15::multiply(a, b); }',
                      [rf'{R}\.L = {R}\.L \* {R}\.L \(T\);']),
    'q15::multiply_nearest': ('opxy::i16 probe(opxy::i16 a, opxy::i16 b) { return opxy::q15::multiply_nearest(a, b); }',
                              [rf'{R}\.L = {R}\.L \* {R}\.L;']),
    'q31::subtract': ('opxy::i32 probe(opxy::i32 a, opxy::i32 b) { return opxy::q31::subtract(a, b); }',
                      [rf'{R} = {R} - {R} \(S\);']),
    'q31::shift_right': ('opxy::i32 probe(opxy::i32 a, opxy::u32 n) { return opxy::q31::shift_right(a, n); }',
                         [rf'{R} >>>= {R};']),
    'q31::multiply': ('opxy::i32 probe(opxy::i32 a, opxy::i32 b) { return opxy::q31::multiply(a, b); }',
                      [rf'{R} = {R} \* {R} \(T\);']),
    'q31::add': ('opxy::i32 probe(opxy::i32 a, opxy::i32 b) { return opxy::q31::add(a, b); }',
                 [rf'{R} = {R} \+ {R} \(S\);']),
    'q31::shift_left': ('opxy::i32 probe(opxy::i32 a, opxy::u32 n) { return opxy::q31::shift_left(a, n); }',
                        [rf'{R} = ASHIFT {R} BY {R}\.L \(S\);']),
    'q31::half': ('opxy::i32 probe(opxy::i32 a) { return opxy::q31::half(a); }', [rf'{R} = {R} >>> 0x1;']),
    # SDK 1.28.
    'q15::half': ('opxy::i16 probe(opxy::i16 a) { return opxy::q15::half(a); }', [rf'{R}\.L = {R}\.L >>> 0x1;']),
    'q15::subtract_wrapped': ('opxy::i16 probe(opxy::i16 a, opxy::i16 b) { return opxy::q15::subtract_wrapped(a, b); }',
                              [r'R0\.L = R0\.L - R1\.L \(NS\);']),
    'q15::pack': ('opxy::u32 probe(opxy::i16 a, opxy::i16 b) { return opxy::q15::pack(a, b); }',
                  [r'R0 = PACK \(R1\.L, R0\.L\);']),
    'q15::multiply_pair': ('opxy::u32 probe(opxy::u32 a, opxy::i16 b) { return opxy::q15::multiply_pair(a, b); }',
                           [r'R0\.H = R0\.H \* R1\.L, R0\.L = R0\.L \* R1\.L \(T\);']),
    'q15::add_pair': ('opxy::u32 probe(opxy::u32 a, opxy::u32 b) { return opxy::q15::add_pair(a, b); }',
                      [rf'{R} = {R} \+\|\+ {R} \(S\);']),
    'q15::high': ('opxy::i16 probe(opxy::u32 a) { return opxy::q15::high(a); }', [r'R0 >>= 0x10;']),
    'q31::shift': ('opxy::i32 probe(opxy::i32 a, opxy::i32 n) { return opxy::q31::shift(a, n); }',
                   [r'R0 = ASHIFT R0 BY R1\.L;']),
    'q15::low': ('opxy::i16 probe(opxy::u32 a) { return opxy::q15::low(a); }', []),
}
# Each helper whose body is one assembly statement giving its result, beside that instruction
# written out as inline assembly that
# declares the arithmetic flags changed: its result and operand types, then the assembly
# with its operands (a and b in, r out, t a scratch register). The strings copy the
# headers' constraints, so the comparison detects a change of constraints, flags or
# scheduling, not a wrong instruction: PROBES and the host references cover that. q31::add alone declares
# only ASTAT, as it has since SDK 1.7: the add never writes CC, and declaring CC now would
# move compares in machines already built with it.
WRITTEN_OUT = {
    'q15::add': ('i16', 'i16', 'i16', '"%0.L = %1.L + %2.L (S);" : "=d"(r) : "d"(a), "d"(b)'),
    'q15::subtract': ('i16', 'i16', 'i16', '"%0.L = %1.L - %2.L (S);" : "=d"(r) : "d"(a), "d"(b)'),
    'q15::normalize': ('i16', 'i16', None,
                       '"%1.L = SIGNBITS %2.L;\\n\\t%0.L = ASHIFT %2.L BY %1.L (S);" : "=d"(r), "=&d"(t) : "d"(a)'),
    'q15::multiply': ('i16', 'i16', 'i16', '"%0.L = %1.L * %2.L (T);" : "=d"(r) : "d"(a), "d"(b)'),
    'q15::multiply_nearest': ('i16', 'i16', 'i16', '"%0.L = %1.L * %2.L;" : "=d"(r) : "d"(a), "d"(b)'),
    'q15::half': ('i16', 'i16', None, '"%0.L = %1.L >>> 1;" : "=d"(r) : "d"(a)'),
    'q15::subtract_wrapped': ('i16', 'i16', 'i16', '"%0.L = %1.L - %2.L;" : "=d"(r) : "d"(a), "d"(b)'),
    'q15::pack': ('u32', 'i16', 'i16', '"%0 = PACK (%1.L, %2.L);" : "=d"(r) : "d"(b), "d"(a)'),
    'q15::multiply_pair': ('u32', 'u32', 'i16',
                           '"%0.L = %1.L * %2.L, %0.H = %1.H * %2.L (T);" : "=d"(r) : "d"(a), "d"(b)'),
    'q15::add_pair': ('u32', 'u32', 'u32', '"%0 = %1 +|+ %2 (S);" : "=d"(r) : "d"(a), "d"(b)'),
    'q31::multiply': ('i32', 'i32', 'i32', '"%0 = %1 * %2 (T);" : "=d"(r) : "d"(a), "d"(b)'),
    'q31::add': ('i32', 'i32', 'i32', '"%0 = %1 + %2 (S);" : "=d"(r) : "d"(a), "d"(b)', '"ASTAT"'),
    'q31::subtract': ('i32', 'i32', 'i32', '"%0 = %1 - %2 (S);" : "=d"(r) : "d"(a), "d"(b)'),
    'q31::shift_left': ('i32', 'i32', 'u32', '"%0 = ASHIFT %1 BY %2.L (S);" : "=d"(r) : "d"(a), "d"(b)'),
    'q31::shift_right': ('i32', 'i32', 'u32', '"%0 >>>= %2;" : "=d"(r) : "0"(a), "d"(b)'),
    'q31::half': ('i32', 'i32', None, '"%0 = %1 >>> 1;" : "=d"(r) : "d"(a)'),
    'q31::shift': ('i32', 'i32', 'i32', '"%0 = ASHIFT %1 BY %2.L;" : "=d"(r) : "d"(a), "d"(b)'),
}
# One call beside a compare, and a loop of them, where the flags a statement declares
# changed shape the schedule: a compare sets CC, so a statement that declares CC changed
# keeps the compare from moving across it.
CONTEXT = """
{result} choose({first} a, {second} b, {result} c) {{
    const {result} x = {op}({arguments});
    return c > {result}(a) ? x : {result}(x + c);
}}
{result} probe({result}* out, const {first}* a, const {second}* b, {result} limit, unsigned n) {{
    {result} kept = limit;
    for (unsigned i = 0; i < n; ++i) {{
        const {result} x = {op}({items});
        out[i] = x;
        kept = x > kept ? x : kept;
        if (a[i] > {first}(kept)) out[i] = kept;
    }}
    return kept;
}}
"""


def written_out(result, first, second, assembly, clobbers='"ASTAT", "CC"'):
    """The instruction as an inline function of plain inline assembly, flags declared changed."""
    operands = f'{first} a' + (f', {second} b' if second else '')
    scratch = ', t' if '(t)' in assembly else ''
    return (f'inline {result} written({operands}) {{\n    {result} r{scratch};\n'
            f'    __asm__({assembly} : {clobbers});\n    return r;\n}}\n')


# A loop using every helper, as a machine's block would.
LOOP = '''
void probe(opxy::i16* out, const opxy::i16* a, const opxy::i16* b, opxy::i32* wide, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        const opxy::i16 x = opxy::q15::add(opxy::q15::multiply_nearest(a[i], b[i]), opxy::q15::multiply(a[i], b[i]));
        out[i] = opxy::q15::normalize(opxy::q15::subtract(x, opxy::q15::saturate(wide[i])));
        wide[i] = opxy::q31::shift_right(opxy::q31::subtract(wide[i], opxy::q31::saturate(opxy::i64(wide[i]) + wide[n - 1 - i])), i & 31);
        wide[i] = opxy::q31::shift(opxy::q31::add(wide[i], opxy::q31::half(wide[i])), opxy::i32(i & 31) - 16);
        const opxy::u32 pair = opxy::q15::multiply_pair(opxy::q15::pack(a[i], b[i]), out[i]);
        out[i] = opxy::q15::half(opxy::q15::subtract_wrapped(opxy::q15::low(opxy::q15::add_pair(pair, pair)), x));
    }
}
'''


HEADERS = '#include <opxy/q15.hpp>\n#include <opxy/q31.hpp>\n'


def require_ok(result, what):
    require(result.returncode == 0, f'{what} failed:\n{result.stdout}{result.stderr}')


def device_listing(source, folder, prelude=HEADERS):
    """The pinned compiler's code for a probe (by default including both headers),
    assembled and listed by the pinned binutils beside it: objdump's canonical spelling."""
    compiler = blackfin_compiler()
    tools = {name: compiler.with_name(compiler.name.replace('g++', name)) for name in ('as', 'objdump')}
    path, assembly, obj = folder / 'probe.cpp', folder / 'probe.s', folder / 'probe.o'
    path.write_text(prelude + source)
    for args, what in (([compiler, *CXX_FLAGS, '-I', ROOT / 'sdk/include', '-S', path, '-o', assembly], 'compile'),
                       ([tools['as'], '-mcpu=bf707', '-o', obj, assembly], 'assemble')):
        require_ok(subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=300, **HIDDEN), what)
    listing = subprocess.run([str(tools['objdump']), '-d', '-t', str(obj)], capture_output=True, text=True,
                             timeout=60, **HIDDEN)
    require_ok(listing, 'objdump')
    return listing.stdout


def instructions(listing):
    """The listed instructions, in order, without their addresses and encodings."""
    return [line.split('\t')[-1].strip() for line in listing.splitlines()
            if re.match(r'\s+[0-9a-f]+:\t', line) and line.count('\t') >= 2]


def mode_accesses(listing):
    """Instructions that read or write ASTAT or its rounding-mode bit."""
    return [text for text in instructions(listing) if re.search(r'\b(ASTAT|RND_MOD)\b', text, re.I)]


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class Host(unittest.TestCase):
    def test_the_host_forms_match_independent_references(self):
        with tempfile.TemporaryDirectory(prefix='fixed-point-') as folder:
            binary = Path(folder) / ('fixed_point' + ('.exe' if os.name == 'nt' else ''))
            build = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-I' + str(ROOT / 'sdk/include'), str(ROOT / 'tests/fixtures/fixed_point.cpp'),
                                    '-o', str(binary)], capture_output=True, text=True, timeout=300, **HIDDEN)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=600, **HIDDEN)
            self.assertEqual((run.returncode, run.stdout.strip()), (0, 'ok'), run.stdout + run.stderr)


@unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
class Device(unittest.TestCase):
    def test_each_helper_is_its_instructions_and_nothing_else(self):
        with tempfile.TemporaryDirectory(prefix='fixed-point-') as folder:
            for name, (source, expected) in PROBES.items():
                with self.subTest(helper=name):
                    listing = device_listing(source, Path(folder))
                    body = instructions(listing)
                    self.assertEqual(body[-1:], ['RTS;'], listing)
                    self.assertEqual(len(body) - 1, len(expected), listing)
                    for text, pattern in zip(body, expected):
                        self.assertRegex(text, '^' + pattern + '$')

    def test_each_helper_compiles_as_its_instruction_written_out(self):
        # The same code around the helper and around its instruction written out must
        # compile to the same instructions, in the same order and registers.
        prelude = HEADERS + 'using namespace opxy;\n'
        with tempfile.TemporaryDirectory(prefix='fixed-point-') as folder:
            for name, (result, first, second, *assembly) in WRITTEN_OUT.items():
                with self.subTest(helper=name):
                    loop = lambda op: CONTEXT.format(result=result, first=first, second=second or 'i32', op=op,
                                                     arguments='a, b' if second else 'a',
                                                     items='a[i], b[i]' if second else 'a[i]')
                    helper = device_listing(loop(name), Path(folder), prelude)
                    written = device_listing(written_out(result, first, second, *assembly) + loop('written'),
                                             Path(folder), prelude)
                    self.assertTrue(instructions(helper), helper)
                    self.assertEqual(instructions(helper), instructions(written), helper + written)

    def test_each_statement_declares_the_flags(self):
        # Read from the headers: every assembly statement declares ASTAT and CC changed,
        # except q31::add (ASTAT only, since SDK 1.7) and q15::product (an integer
        # multiply, none since it was added).
        found = []
        for header in ('q15', 'q31'):
            text = (ROOT / f'sdk/include/opxy/{header}.hpp').read_text()
            for match in re.finditer(r'__asm__\("((?:[^"\\]|\\.)*)"\s*((?:"[^"]*"|[^;])*?)\);', text):
                if not match[2].strip():
                    continue  # a register variable's name, not a statement
                parts = match[2].split(':')
                found.append((header, match[1], parts[3].strip() if len(parts) > 3 else ''))
        self.assertEqual(len(found), 20, found)
        exceptions = {('q31', '%0 = %1 + %2 (S);'): '"ASTAT"', ('q15', '%0 = %1.L * %2.L (IS);'): ''}
        for header, statement, clobbers in found:
            with self.subTest(statement=(header, statement)):
                self.assertEqual(clobbers, exceptions.get((header, statement), '"ASTAT", "CC"'))

    def test_the_comparison_sees_the_flags_declared(self):
        # The control: the same add written out declaring CC as well moves a compare in
        # this code. It shows the comparison can see a flag; for most helpers this code
        # does not depend on the flags, which the test above checks directly.
        prelude = HEADERS + 'using namespace opxy;\n'
        result, first, second, assembly, _ = WRITTEN_OUT['q31::add']
        loop = lambda op: CONTEXT.format(result=result, first=first, second=second, op=op, arguments='a, b',
                                         items='a[i], b[i]')
        with tempfile.TemporaryDirectory(prefix='fixed-point-') as folder:
            helper = device_listing(loop('q31::add'), Path(folder), prelude)
            both = device_listing(written_out(result, first, second, assembly) + loop('written'), Path(folder), prelude)
        self.assertNotEqual(instructions(helper), instructions(both), helper)

    def test_the_wide_saturate_is_compiled_code_without_a_call(self):
        with tempfile.TemporaryDirectory(prefix='fixed-point-') as folder:
            listing = device_listing('opxy::i32 probe(opxy::i64 x) { return opxy::q31::saturate(x); }', Path(folder))
        self.assertTrue(instructions(listing), listing)
        self.assertFalse([t for t in instructions(listing) if re.match(r'(CALL|JUMP\.L|JUMP \(P)', t, re.I)], listing)
        self.assertEqual(mode_accesses(listing), [], listing)

    def test_a_loop_of_them_never_touches_the_rounding_mode_or_calls(self):
        with tempfile.TemporaryDirectory(prefix='fixed-point-') as folder:
            listing = device_listing(LOOP, Path(folder))
        body = instructions(listing)
        self.assertEqual(mode_accesses(listing), [], listing)
        self.assertFalse([t for t in body if t.upper().startswith('CALL')], listing)
        # The rounding multiply stays one plain multiply, not a truncating or integer one.
        self.assertEqual(len([t for t in body if re.fullmatch(rf'{R}\.L = {R}\.L \* {R}\.L;', t)]), 1, listing)

    def test_the_mode_check_sees_every_spelling_of_an_access(self):
        # The control (an ordinary add) must pass; each other template must be found,
        # whatever its case, and also when it is written as its raw encoding.
        spellings = {'ASTAT = %0;': 1, 'astat = %0;': 1, 'RND_MOD = CC;': 1, 'rnd_mod = cc;': 1,
                     '%0 = ASTAT;': 1, '.short 0x0388;': 1, '%0 = %0 + %0;': 0}
        with tempfile.TemporaryDirectory(prefix='fixed-point-') as folder:
            for template, expected in spellings.items():
                with self.subTest(template=template):
                    source = ('opxy::i16 probe(opxy::i16 a, opxy::i16 b) {\n    opxy::i32 x = a;\n'
                              '    __asm__ volatile("' + template + '" : "+d"(x));\n'
                              '    return opxy::q15::multiply_nearest(opxy::i16(x), b);\n}\n')
                    self.assertEqual(len(mode_accesses(device_listing(source, Path(folder)))), expected)

    def test_including_the_headers_adds_no_code(self):
        # Inline helpers that nothing calls emit nothing: a unit with both headers
        # compiles to the same code and symbols as one without them.
        plain = 'opxy::i32 probe(opxy::i32 x) { return x + 1; }\n'
        with tempfile.TemporaryDirectory(prefix='fixed-point-') as folder:
            with_headers = device_listing(plain, Path(folder))
            without = device_listing(plain, Path(folder), prelude='#include <opxy/types.hpp>\n')
        self.assertTrue(instructions(with_headers), with_headers)
        self.assertEqual(with_headers, without)


if __name__ == '__main__':
    unittest.main()
