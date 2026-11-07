"""Contributor source boundaries. Platform mechanisms belong to the SDK."""
import re

from .formats import require



def validate_source_boundary(source, name='<engine>'):
    # This is an architectural lint, not a C++ parser or a security sandbox.
    # Strip comments so explanatory text does not count as an implementation.
    code = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    require(not re.search(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b', code, re.M),
            name + ': conditional compilation belongs in SDK support code')
    require(not re.search(r'\b(_WIN32|__bfin__|__asm__|asm)\b', code),
            name + ': platform/compiler primitives belong in SDK support code')
    require(not re.search(r'#\s*include\s*[<"](?:windows\.h|io\.h|fcntl\.h)[>"]', code),
            name + ': OS headers belong in SDK support code')


# A machine's native routines (native/<name>.s, docs/api.md "Native routines"):
# plain GNU assembler for the BF707 core, never preprocessed. Like the C++ lint,
# these are cheap architectural checks of the text, not a proof of behaviour or a
# sandbox: the build checks the assembled object again (tooling/native.py), and
# the C reference beside each routine defines what it must compute.
NATIVE_DIRECTIVES = {'section', 'text', 'data', 'bss', 'align', 'p2align', 'balign', 'global', 'globl', 'type', 'size',
                     'long', 'int', 'short', 'hword', 'byte', '2byte', '4byte', 'zero', 'space', 'skip', 'fill',
                     'ascii', 'asciz', 'string', 'equ', 'set', 'rept', 'endr', 'macro', 'endm'}
NATIVE_CODE_SECTIONS = {'.text', '.rodata'}
NATIVE_STATE_SECTIONS = {'.data', '.bss'}
# Supervisor, exception, interrupt and cache control, and the system registers:
# the firmware owns these, and audio code never needs them.
NATIVE_PRIVILEGED = {'RAISE', 'EXCPT', 'IDLE', 'CLI', 'STI', 'RTI', 'RTX', 'RTN', 'RTE', 'EMUEXCPT', 'TESTSET',
                     'FLUSH', 'FLUSHINV', 'IFLUSH', 'SYSCFG', 'SEQSTAT', 'RETI', 'RETX', 'RETN', 'RETE', 'USP', 'EMUDAT'}
_CONSTANT = r'[-+]?\s*(?:0x[0-9a-f]+|\d+)'


def assembly_statements(source):
    """The statements of a GNU as source for Blackfin, labels removed: comments stripped
    (C-style, // and # lines), then split at newlines and at each instruction's ';'."""
    code = re.sub(r'/\*.*?\*/', ' ', source, flags=re.S)
    code = re.sub(r'//[^\n]*|^\s*#[^\n]*', '', code, flags=re.M)
    statements = []
    for line in code.splitlines():
        for part in line.split(';'):
            part = part.strip()
            while (label := re.match(r'(?:[A-Za-z_.$][\w.$]*|\d+):\s*', part)) is not None:
                part = part[label.end():]
            if part:
                statements.append(part)
    return statements


def _integer(text):
    try:
        return int(text.replace(' ', ''), 0)
    except ValueError:
        return None


def validate_native_assembly(source, name, prefix, symbol, state=False):
    """A native unit's text: its one exported symbol, its sections and the instructions
    it may use. `state` admits .data and .bss, which its manifest declares (stateBytes)."""
    sections = NATIVE_CODE_SECTIONS | (NATIVE_STATE_SECTIONS if state else set())
    exported, zeroed, loaded = [], set(), set()
    require(not any('\\' in statement for statement in assembly_statements(source)),
            f'{name}: no macro argument substitution (\\)')
    for statement in assembly_statements(source):
        if directive := re.match(r'\.(\S+)\s*(.*)$', statement):
            kind, operands = directive.group(1).lower(), directive.group(2).strip()
            require(kind in NATIVE_DIRECTIVES, f'{name}: the .{kind} directive is not allowed in a native unit')
            # A macro repeats the statements checked here; arguments would let it build others.
            require(kind != 'macro' or re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', operands) is not None,
                    f'{name}: a native unit\'s macros take no arguments')
            if kind == 'section':
                require(operands in sections, f'{name}: a native unit has only {", ".join(sorted(sections))} sections, '
                        'named without flags' + ('' if state else ' (.data and .bss need stateBytes in its manifest)'))
            elif kind in ('data', 'bss'):
                require('.' + kind in sections, f'{name}: .{kind} needs stateBytes in its manifest')
            elif kind in ('global', 'globl'):
                exported += [s.strip() for s in operands.split(',')]
            continue
        privileged = sorted(set(re.findall(r'[A-Z_][A-Z0-9_]*', statement.upper())) & NATIVE_PRIVILEGED)
        require(not privileged, f'{name}: {" ".join(privileged)} is supervisor or system control, which the firmware owns')
        require(not re.search(r'\bCYCLES2?\s*=(?!=)', statement, re.I),
                f'{name}: the cycle counters belong to the SDK\'s profiling')
        require(not re.search(r'\bCALL\s*\(', statement, re.I),
                f'{name}: an indirect CALL hides its callee from the stack analysis')
        # The stack moves only in the forms the stack analysis measures.
        require(not re.search(r'\bSP\s*(?:[-*]=|=(?!=)|\+=(?!\s*' + _CONSTANT + r'\s*$))', statement, re.I) and
                not re.search(r'SP\s*--|\+\+\s*SP|SP\s*\+\+\s*[A-Z]|\bSP\.[HL]\b', statement, re.I),
                f'{name}: change SP only by push, pop, LINK/UNLINK or SP += constant: {statement}')
        # Pointer and index registers never take a memory-mapped register's address
        # (0xFFC00000 up) by halves: system and core registers are the firmware's.
        # The high half is a constant, or a constant's HI(), which takes its upper 16 bits.
        if mmr := re.match(r'(?:P[0-5]|I[0-3]|SP|FP)\.H\s*=\s*(?:HI\s*\(\s*(' + _CONSTANT + r')\s*\)|(' + _CONSTANT +
                           r'))\s*(?:\(\s*[XZ]\s*\))?$', statement, re.I):
            value = _integer(mmr.group(2)) if mmr.group(1) is None else (_integer(mmr.group(1)) or 0) >> 16
            require(value is None or value & 0xffff < 0xffc0,
                    f'{name}: memory-mapped registers are the firmware\'s: {statement}')
        # A write to one half keeps the other: only a whole-register 0 clears an L register.
        if circular := re.match(r'L([0-3])(\.[HL])?\s*=\s*(.+)$', statement, re.I):
            cleared = circular.group(2) is None and _integer(circular.group(3)) == 0
            (zeroed if cleared else loaded).add(circular.group(1))
    require(re.fullmatch(re.escape(prefix) + r'_native_[a-z0-9_]+', symbol) is not None,
            f'{name}: its symbol starts with {prefix}_native_, the machine\'s own prefix')
    require(exported == ['_' + symbol], f'{name}: a native unit exports exactly its manifest\'s symbol, '
            f'_{symbol}' + (f', not {", ".join(exported)}' if exported else ''))
    unreset = sorted(loaded - zeroed)
    require(not unreset, f'{name}: L{unreset[0] if unreset else ""} must be 0 again before it returns '
            '(compiled code relies on L0..L3 being 0)')
