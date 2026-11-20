"""A small model of the Blackfin instructions the SDK's hand-written runtime helpers use
(sdk/platform/blackfin/divide.s and the device half of memory.cpp), for host tests.

It reads the helpers' own assembly text and runs it, so a test exercises the code that
ships. It is independent of the code under test: its semantics come from the Blackfin
Processor Programming Reference (and, for DIVS/DIVQ, the GNU simulator's reading of it),
not from the helpers. It knows only the instruction forms those files use and rejects
any other, so a new form in either file needs a matching, reviewed addition here.
Nothing in it runs Blackfin machine code: assembler and encoding errors are the pinned
toolchain's to catch (tests/test_runtime_helpers.py assembles both files when present).
"""
import re

MASK = 0xffffffff
# Registers a callee must preserve under GCC's Blackfin convention (see Machine.call).
CALLEE_SAVED = ('R4', 'R5', 'R6', 'R7', 'P3', 'P4', 'P5', 'FP', 'SP')
REG = r'(R[0-7]|P[0-5]|SP|RETS)'


def signed(value):
    value &= MASK
    return value - (1 << 32) if value & 0x80000000 else value


def statements(text):
    """(label or None, statement) pairs from assembly text, comments and directives removed."""
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    out = []
    for line in text.split('\n'):
        line = line.strip()
        while True:
            match = re.match(r'([A-Za-z_.$][\w.$]*):\s*(.*)$', line)
            if not match:
                break
            out.append((match.group(1), None))
            line = match.group(2).strip()
        for part in line.split(';'):
            part = ' '.join(part.split())
            if part and not part.startswith('.'):
                out.append((None, part))
    return out


class Machine:
    def __init__(self, text, memory_base=0x1000, memory_size=0x1000):
        self.program, self.labels = [], {}
        for label, statement in statements(text):
            if label is not None:
                self.labels[label] = len(self.program)
            else:
                self.program.append(statement)
        self.base = memory_base
        self.memory = bytearray(memory_size)
        self.reg = {name: 0 for name in [f'R{i}' for i in range(8)] + [f'P{i}' for i in range(6)] + ['FP', 'SP', 'RETS']}
        self.cc = self.aq = 0
        self.loops = {}  # end index -> [start index, count register]
        self.lc = {}
        self.steps = 0

    # Memory, with the device's alignment rule for half-word and word accesses.
    def _at(self, address, size):
        address &= MASK
        assert address % size == 0, f'misaligned {size}-byte access at {address:#x}'
        offset = address - self.base
        assert 0 <= offset and offset + size <= len(self.memory), f'access outside memory at {address:#x}'
        return offset

    def load(self, address, size):
        offset = self._at(address, size)
        return int.from_bytes(self.memory[offset:offset + size], 'little')

    def store(self, address, size, value):
        offset = self._at(address, size)
        self.memory[offset:offset + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, 'little')

    def get(self, name):
        return self.reg[name] & MASK

    def set(self, name, value):
        self.reg[name] = value & MASK

    def call(self, label, limit=100000, **registers):
        """Runs `label` as a compiled caller would call it: a fresh register state (every
        register other than the arguments poisoned with its own value, CC, AQ and loop
        state cleared), the arguments, then the callee-saved registers checked on return.

        The convention is GCC's for Blackfin, which compiled machine code uses: R0-R3,
        P0-P2, CC/ASTAT and the hardware-loop registers are the callee's to clobber;
        R4-R7, P3-P5, FP and SP must come back unchanged. The pinned toolchain's own
        output shows it: functions save R4-R7/P3-P5 in their prologues and never save
        LC0/LC1 before a hardware loop (tests/test_runtime_helpers.py)."""
        for index, name in enumerate(self.reg):
            self.set(name, 0xa5a50000 + index)
        for name, value in registers.items():
            self.set(name, value)
        self.set('SP', self.base + len(self.memory))
        self.set('RETS', 0xdead0000)
        saved = {name: self.get(name) for name in CALLEE_SAVED}
        pc = self.labels[label]
        self.cc = self.aq = 0
        self.loops, self.lc, self.steps = {}, {}, 0
        while True:
            self.steps += 1
            assert self.steps < limit, 'step limit'
            target = self.execute(pc)
            if target == 'return':
                if self.get('RETS') == 0xdead0000:
                    for name, value in saved.items():
                        assert self.get(name) == value, f'callee-saved {name} changed'
                    return self
                pc = self.get('RETS')
                continue
            if target is not None:
                pc = target
                continue
            # A hardware loop's bottom: back to its top while its count is above 1.
            loop = self.loops.get(pc)
            if loop is not None and self.lc[loop[1]] > 1:
                self.lc[loop[1]] -= 1
                pc = loop[0]
            else:
                pc += 1

    def execute(self, pc):
        s = self.program[pc]
        g, st = self.get, self.set
        m = re.fullmatch
        if s == 'RTS':
            return 'return'
        if (x := m(r'CALL (\S+)', s)):
            st('RETS', pc + 1)
            return self.labels[x.group(1)]
        if (x := m(r'JUMP(?:\.S)? (\S+)', s)):
            return self.labels[x.group(1)]
        if (x := m(r'IF (!?)CC JUMP (\S+)', s)):
            return self.labels[x.group(2)] if bool(self.cc) != bool(x.group(1)) else None
        if (x := m(r'IF CC (R[0-7]) = (R[0-7])', s)):
            if self.cc:
                st(x.group(1), g(x.group(2)))
            return None
        if (x := m(r'LSETUP \((\S+), (\S+)\) (LC[01]) = (P[0-5])', s)):
            count = g(x.group(4))
            assert count >= 1, 'hardware loop count 0 runs 2^32 times'
            self.loops = {end: loop for end, loop in self.loops.items() if loop[1] != x.group(3)}
            self.loops[self.labels[x.group(2)]] = [self.labels[x.group(1)], x.group(3)]
            self.lc[x.group(3)] = count
            return None
        if (x := m(r'DIVS \((R[0-7]), (R[0-7])\)', s)):
            dividend, divisor = g(x.group(1)), g(x.group(2)) & 0xffff
            self.aq = ((dividend >> 31) ^ (divisor >> 15)) & 1
            st(x.group(1), (dividend << 1) | self.aq)
            return None
        if (x := m(r'DIVQ \((R[0-7]), (R[0-7])\)', s)):
            dividend, divisor = g(x.group(1)), g(x.group(2)) & 0xffff
            high = ((dividend >> 16) + divisor if self.aq else (dividend >> 16) - divisor) & 0xffff
            self.aq = ((high >> 15) ^ (divisor >> 15)) & 1
            st(x.group(1), (((high << 16) | (dividend & 0xffff)) << 1) | (self.aq ^ 1))
            return None
        # Condition codes.
        if (x := m(r'CC = (R[0-7]) (<=|<) (R[0-7]) \(IU\)', s)):
            a, b = g(x.group(1)), g(x.group(3))
            self.cc = int(a <= b if x.group(2) == '<=' else a < b)
            return None
        if (x := m(r'CC = (R[0-7]) == 0', s)):
            self.cc = int(g(x.group(1)) == 0)
            return None
        if (x := m(r'CC = (R[0-7]) < 0', s)):
            self.cc = int(signed(g(x.group(1))) < 0)
            return None
        if (x := m(r'CC = BITTST \((R[0-7]), (\d+)\)', s)):
            self.cc = (g(x.group(1)) >> int(x.group(2))) & 1
            return None
        # Memory.
        size = {'B': 1, 'W': 2, '': 4}
        if (x := m(r'(B|W|)\[(P[0-5])(\+\+)?\] = (R[0-7])', s)):
            n = size[x.group(1)]
            self.store(g(x.group(2)), n, g(x.group(4)))
            if x.group(3):
                st(x.group(2), g(x.group(2)) + n)
            return None
        if (x := m(r'(R[0-7]) = (B|W|)\[(P[0-5])(\+\+)?\](?: \((Z|X)\))?', s)):
            n = size[x.group(2)]
            value = self.load(g(x.group(3)), n)
            if x.group(5) == 'X' and value >> (8 * n - 1):
                value -= 1 << (8 * n)
            assert n == 4 or x.group(5), 'a byte or half-word load names its extension'
            st(x.group(1), value)
            if x.group(4):
                st(x.group(3), g(x.group(3)) + n)
            return None
        if (x := m(r'\[--SP\] = (R[0-7]|RETS)', s)):
            st('SP', g('SP') - 4)
            self.store(g('SP'), 4, g(x.group(1)))
            return None
        if (x := m(r'(R[0-7]|RETS) = \[SP\+\+\]', s)):
            st(x.group(1), self.load(g('SP'), 4))
            st('SP', g('SP') + 4)
            return None
        # Arithmetic.
        if (x := m(r'(R[0-7])\.L = SIGNBITS (R[0-7])', s)):
            v = g(x.group(2))
            v = v ^ MASK if v >> 31 else v
            bits = 31 - v.bit_length()  # redundant sign bits
            st(x.group(1), (g(x.group(1)) & 0xffff0000) | bits)
            return None
        if (x := m(r'(R[0-7]) = ROT (R[0-7]) BY 1', s)):
            v = g(x.group(2))
            st(x.group(1), (v << 1) | self.cc)
            self.cc = v >> 31
            return None
        if (x := m(r'(R[0-7]) = (R[0-7])\.(L|B) \(Z\)', s)):
            st(x.group(1), g(x.group(2)) & (0xffff if x.group(3) == 'L' else 0xff))
            return None
        if (x := m(r'(R[0-7]) = -(R[0-7])', s)):
            st(x.group(1), -g(x.group(2)))
            return None
        if (x := m(r'(R[0-7]) = (R[0-7]) ([-+|^&]) (R[0-7])', s)):
            a, b = g(x.group(2)), g(x.group(4))
            st(x.group(1), {'-': a - b, '+': a + b, '|': a | b, '^': a ^ b, '&': a & b}[x.group(3)])
            return None
        if (x := m(r'(R[0-7]) = (R[0-7]) (<<|>>) (\d+)', s)):
            a, n = g(x.group(2)), int(x.group(4))
            st(x.group(1), a << n if x.group(3) == '<<' else a >> n)
            return None
        if (x := m(r'(R[0-7]) (<<|>>)= (\d+|R[0-7])', s)):
            a = g(x.group(1))
            n = g(x.group(3)) if x.group(3).startswith('R') else int(x.group(3))
            st(x.group(1), 0 if n >= 32 else (a << n if x.group(2) == '<<' else a >> n))
            return None
        if (x := m(r'(R[0-7]) \+= (-?\d+)', s)):
            st(x.group(1), g(x.group(1)) + int(x.group(2)))
            return None
        if (x := m(r'(R[0-7]) \*= (R[0-7])', s)):
            st(x.group(1), g(x.group(1)) * g(x.group(2)))
            return None
        if (x := m(r'(R[0-7]) = (-?(?:0x[0-9a-fA-F]+|\d+))(?: \((X|Z)\))?', s)):
            value = int(x.group(2), 0)
            if x.group(3) == 'X':
                assert -32768 <= value <= 32767
            if x.group(3) == 'Z':
                assert 0 <= value <= 0xffff
            st(x.group(1), value)
            return None
        if (x := m(REG + r' = ' + REG, s)):
            st(x.group(1), g(x.group(2)))
            return None
        raise AssertionError(f'instruction form not in the model: {s!r}')
