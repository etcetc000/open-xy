"""Lazy arithmetic helpers from the authenticated target compiler archive.

No allocator, unwinder or operating-system support is admitted. The native and
browser linkers select the same dependency closure, in member-name order.
"""
import re
import struct

from .formats import elf_sections, require

ARCHIVE = 'lib/gcc/bfin-elf/8.0.0/libgcc.a'
# Reviewed integer and IEEE binary32/binary64 support in the pinned compiler.
# The SDK's own division definitions take precedence over archive members.
MEMBERS = frozenset('''
_addsub_df _addsub_sf _clz _clzsi2 _df_to_sf _df_to_si _div_df _div_sf
_divdi3 _divsi3 _eq_df _eq_sf _fixdfdi _fixsfdi _fixunsdfdi _fixunsdfsi
_fixunssfdi _fixunssfsi _floatdidf _floatdisf _floatundidf _floatundisf
_fpcmp_parts_df _fpcmp_parts_sf _ge_df _ge_sf _gt_df _gt_sf _le_df _le_sf
_lt_df _lt_sf _make_df _make_sf _moddi3 _mul_df _mul_sf _muldi3 _ne_df
_ne_sf _negate_df _negate_sf _pack_df _pack_sf _sf_to_df _sf_to_si
_si_to_df _si_to_sf _thenan_df _thenan_sf _udivdi3 _udivsi3 _umoddi3
_umodsi3 _unpack_df _unpack_sf _unord_df _unord_sf _usi_to_df _usi_to_sf
'''.split())
PREFIX = 'sdk/runtime/'


def object_name(member):
    require(member.endswith('.o') and member[:-2] in MEMBERS, 'Unreviewed arithmetic member: ' + member)
    return PREFIX + member


def archive_members(data):
    """Read bounded GNU ar members without extracting paths or trusting its index."""
    require(8 <= len(data) <= 16 * 1024 * 1024 and data[:8] == b'!<arch>\n', 'Invalid runtime archive')
    at, table, result, records = 8, None, {}, 0
    while at < len(data):
        records += 1
        require(records <= 4096 and at + 60 <= len(data), 'Invalid archive member boundary')
        header = data[at:at + 60]
        require(header[58:] == b'`\n', 'Invalid archive member header')
        try:
            name = header[:16].decode('ascii').rstrip()
            size_text = header[48:58].decode('ascii').strip()
        except UnicodeDecodeError:
            raise ValueError('Non-ASCII archive header') from None
        require(size_text.isdecimal(), 'Invalid archive member size')
        size = int(size_text)
        end = at + 60 + size
        require(end + size % 2 <= len(data), 'Archive member exceeds file')
        if size % 2:
            require(data[end] == 10, 'Invalid archive padding')
        body = data[at + 60:end]
        if name == '//':
            require(table is None, 'Duplicate archive name table')
            table = body
        elif name != '/':
            if re.fullmatch(r'/[0-9]+', name):
                offset = int(name[1:])
                require(table is not None and offset < len(table) and
                        (offset == 0 or table[offset - 1] == 10), 'Invalid archive name reference')
                stop = table.find(b'/\n', offset)
                require(stop >= offset, 'Unterminated archive member name')
                try:
                    name = table[offset:stop].decode('ascii')
                except UnicodeDecodeError:
                    raise ValueError('Non-ASCII archive member name') from None
            else:
                require(name.endswith('/'), 'Unsupported archive name encoding')
                name = name[:-1]
            require(re.fullmatch(r'[A-Za-z0-9_.-]+\.o', name) is not None and name not in result,
                    'Unsafe or duplicate archive member name')
            result[name] = body
        at = end + size % 2
    return result


def symbols(data):
    """Strong global definitions and imports of one bounded ELF32 object."""
    require(52 <= len(data) <= 1024 * 1024 and struct.unpack_from('<H', data, 16)[0] == 1,
            'Expected a bounded relocatable object')
    headers = [h for _at, h in elf_sections(data)]
    tables = [h for h in headers if h[1] == 2]
    require(len(tables) == 1, 'Expected one object symbol table')
    table = tables[0]
    require(table[9] == 16 and table[5] % 16 == 0 and table[6] < len(headers) and
            table[4] + table[5] <= len(data), 'Invalid object symbol table')
    strings = headers[table[6]]
    require(strings[1] == 3 and strings[4] + strings[5] <= len(data), 'Invalid object string table')
    text = data[strings[4]:strings[4] + strings[5]]
    defined, undefined = set(), set()
    for at in range(table[4], table[4] + table[5], 16):
        offset, _value, _size, info, _other, section = struct.unpack_from('<IIIBBH', data, at)
        require(section < len(headers) or section == 0xfff1, 'COMMON and extended symbols are unsupported')
        require(offset < len(text), 'Object symbol name outside string table')
        end = text.find(b'\0', offset)
        require(end >= offset, 'Unterminated object symbol')
        if not info >> 4 or info & 15 == 4:
            continue
        require(info >> 4 in (1, 2), 'Unsupported object symbol binding')
        name = text[offset:end].decode('ascii')
        if name:
            (defined if section else undefined).add(name)
    return defined, undefined


def needs_runtime(objects, absolute=()):
    defined, undefined = set(absolute), set()
    for data in objects:
        exports, imports = symbols(data)
        defined.update(exports)
        undefined.update(imports)
    return any(re.fullmatch(r'___(?:add|sub|mul|div|mod|udiv|umod|fix|float|extend|trunc|eq|ne|ge|gt|le|lt|unord|neg|cmp|clz|ctz|ashl|ashr|lshr)[A-Za-z0-9_]*', name)
               for name in undefined - defined)


def normalize_member(data):
    """Discard only non-executable metadata exactly as the module linker does.

    ELF indexes and code/data/relocation bytes stay intact. Relocations of
    discarded metadata become empty PROGBITS so the browser cannot apply them.
    """
    symbols(data)
    sections = elf_sections(data)
    symbol_index = next(i for i, (_at, h) in enumerate(sections) if h[1] == 2)
    symbol_count = sections[symbol_index][1][5] // 16
    names = sections[struct.unpack_from('<H', data, 50)[0]][1]
    require(names[4] + names[5] <= len(data), 'Invalid section name table')
    strings = data[names[4]:names[4] + names[5]]
    out, allocated = bytearray(data), set()
    for index, (at, h) in enumerate(sections):
        require(h[0] < len(strings), 'Invalid section name')
        stop = strings.find(b'\0', h[0])
        require(stop >= h[0], 'Unterminated section name')
        name = strings[h[0]:stop].decode('ascii')
        alignment = h[8] or 1
        require(h[5] <= 1024 * 1024 and alignment <= 4096 and not alignment & (alignment - 1),
                'Invalid runtime section size/alignment')
        require(h[1] == 8 or h[4] + h[5] <= len(data), 'Object section exceeds file')
        if not h[2] & 2:
            continue
        if name in ('.eh_frame', '.eh_frame_hdr'):
            struct.pack_into('<I', out, at + 8, 0)
            struct.pack_into('<I', out, at + 20, 0)
            continue
        require(re.fullmatch(r'\.(?:text|rodata|data|sdata|bss|sbss)(?:\..*)?', name) is not None and
                h[1] == (8 if re.match(r'\.(?:bss|sbss)(?:\.|$)', name) else 1) and not h[2] & ~0x37,
                'Unsupported runtime section: ' + name)
        struct.pack_into('<I', out, at + 8, h[2] & ~0x30)
        allocated.add(index)
    touched = set()
    for at, h in sections:
        require(h[1] != 9, 'REL without explicit addends is unsupported')
        if h[1] != 4:
            continue
        require(h[7] < len(sections), 'Invalid runtime relocation target')
        if h[7] not in allocated:
            struct.pack_into('<I', out, at + 4, 1)
            struct.pack_into('<I', out, at + 20, 0)
            continue
        require(h[6] == symbol_index and h[9] == 12 and h[5] % 12 == 0,
                'Invalid runtime relocation table')
        target = sections[h[7]][1]
        require(target[1] == 1, 'Runtime relocation target is not initialized memory')
        for pos in range(h[4], h[4] + h[5], 12):
            offset, info = struct.unpack_from('<II', data, pos)
            kind, symbol = info & 255, info >> 8
            require(kind in (6, 7, 10, 13, 18) and symbol < symbol_count, 'Unsupported runtime relocation')
            width = 4 if kind == 18 else 2
            require(offset % 2 == 0 and offset + width <= target[5] and
                    (kind not in (10, 13) or offset >= 2), 'Runtime relocation exceeds section')
            region = {(h[7], byte) for byte in range(offset - (2 if kind in (10, 13) else 0), offset + width)}
            require(not region & touched, 'Overlapping runtime relocations')
            touched.update(region)
    return bytes(out)


def select(objects, candidates, absolute=()):
    """Select a deterministic arithmetic closure; unknown imports remain errors at link."""
    defined, undefined = set(absolute), set()
    for data in objects:
        exports, imports = symbols(data)
        defined.update(exports)
        undefined.update(imports)
    entries = {}
    for name, data in candidates.items():
        object_name(name)
        entries[name] = symbols(data)
    chosen = set()
    while True:
        pending = undefined - defined
        additions = [name for name in sorted(entries) if name not in chosen and entries[name][0] & pending]
        if not additions:
            break
        for name in additions:
            exports, imports = entries[name]
            require(not exports & defined, 'Runtime member replaces an existing definition: ' + name)
            chosen.add(name)
            defined.update(exports)
            undefined.update(imports)
    return [name for name in sorted(chosen)]
