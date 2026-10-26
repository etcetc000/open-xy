"""Arithmetic runtime selection uses authored ELF/archive fixtures only."""
import struct
import unittest

from tooling import runtime
from tooling.formats import elf_sections


def object_bytes(exports=(), imports=(), section='.text', flags=6):
    strings = b'\0'
    symbols = bytes(16)
    for name, defined in [(n, True) for n in exports] + [(n, False) for n in imports]:
        symbols += struct.pack('<IIIBBH', len(strings), 0, 0, 0x12, 0, int(defined))
        strings += name.encode('ascii') + b'\0'
    names = b'\0' + section.encode('ascii') + b'\0.symtab\0.strtab\0.shstrtab\0'
    code = b'\x00\x60\x10\x00'
    sym_at, str_at = 56, 56 + len(symbols)
    names_at = str_at + len(strings)
    table = (names_at + len(names) + 3) & ~3
    data = bytearray(table + 200)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHI', data, 16, 1, 106, 1)
    struct.pack_into('<I', data, 32, table)
    struct.pack_into('<HHHHHH', data, 40, 52, 0, 0, 40, 5, 4)
    data[52:56], data[sym_at:str_at] = code, symbols
    data[str_at:names_at] = strings
    data[names_at:names_at + len(names)] = names
    for i, h in enumerate([
            [0] * 10,
            [1, 1, flags, 0, 52, 4, 0, 0, 4, 0],
            [names.index(b'.symtab'), 2, 0, 0, sym_at, len(symbols), 3, 1, 4, 16],
            [names.index(b'.strtab'), 3, 0, 0, str_at, len(strings), 0, 0, 1, 0],
            [names.index(b'.shstrtab'), 3, 0, 0, names_at, len(names), 0, 0, 1, 0]]):
        struct.pack_into('<10I', data, table + i * 40, *h)
    return bytes(data)


def member(name, body):
    return (name.ljust(16) + '0'.ljust(12) + '0'.ljust(6) + '0'.ljust(6) +
            '100644'.ljust(8) + str(len(body)).ljust(10) + '`\n').encode('ascii') + body + (b'\n' if len(body) % 2 else b'')


def relocated(offset=0, kind=18, symbol=1, target=1, link=2):
    data = bytearray(object_bytes(['entry']))
    headers = [h for _at, h in elf_sections(data)]
    at = len(data)
    data += struct.pack('<IIi', offset, (symbol << 8) | kind, 0)
    table = len(data)
    headers.append((0, 4, 0, 0, at, 12, link, target, 4, 12))
    data += b''.join(struct.pack('<10I', *h) for h in headers)
    struct.pack_into('<I', data, 32, table)
    struct.pack_into('<H', data, 48, len(headers))
    return bytes(data)


class RuntimeTests(unittest.TestCase):
    def test_archive_short_and_long_names(self):
        data = b'!<arch>\n' + member('/', b'index') + member('//', b'_floatundidf.o/\n')
        data += member('_muldi3.o/', b'one') + member('/0', b'two')
        self.assertEqual(runtime.archive_members(data), {'_muldi3.o': b'one', '_floatundidf.o': b'two'})

    def test_archive_rejects_bounds_paths_duplicate_names_and_padding(self):
        valid = b'!<arch>\n' + member('_muldi3.o/', b'one')
        invalid = [valid[:-1], valid[:-1] + b'X', valid + b'x',
                   b'!<arch>\n' + member('../bad.o/', b''),
                   valid + member('_muldi3.o/', b''),
                   b'!<arch>\n' + member('/0', b''),
                   b'!<arch>\n' + member('//', b'foo.o/\n') + member('/1', b'')]
        for data in invalid:
            with self.subTest(data=data), self.assertRaises(ValueError):
                runtime.archive_members(data)

    def test_lazy_closure_is_sorted_and_preserves_existing_division(self):
        candidates = {
            '_muldi3.o': object_bytes(['___muldi3'], ['___divsi3', '___clzsi2']),
            '_divsi3.o': object_bytes(['___divsi3']),
            '_clzsi2.o': object_bytes(['___clzsi2']),
        }
        regular = [object_bytes(['___divsi3'], ['___muldi3'])]
        self.assertEqual(runtime.select(regular, candidates), ['_clzsi2.o', '_muldi3.o'])
        self.assertEqual(runtime.select([object_bytes()], candidates), [])
        self.assertEqual(runtime.select(regular, candidates, ['___muldi3']), [])
        self.assertTrue(runtime.needs_runtime(regular))
        self.assertFalse(runtime.needs_runtime(regular, ['___muldi3']))
        self.assertFalse(runtime.needs_runtime([object_bytes(imports=['_malloc'])]))

    def test_collision_and_unreviewed_runtime_fail_closed(self):
        with self.assertRaisesRegex(ValueError, 'replaces'):
            runtime.select([object_bytes(['owned'], ['___muldi3'])],
                           {'_muldi3.o': object_bytes(['owned', '___muldi3'])})
        with self.assertRaisesRegex(ValueError, 'Unreviewed'):
            runtime.select([], {'malloc.o': object_bytes()})

    def test_normalization_preserves_code_and_discards_only_allowed_flags(self):
        source = object_bytes(['___muldi3'], flags=0x36)
        normalized = runtime.normalize_member(source)
        self.assertEqual(normalized[52:56], source[52:56])
        self.assertEqual(elf_sections(normalized)[1][1][2], 6)
        eh = runtime.normalize_member(object_bytes(section='.eh_frame', flags=2))
        self.assertEqual(elf_sections(eh)[1][1][2:6], (0, 0, 52, 0))
        for section, flags in [('.init_array', 3), ('.text', 0x406)]:
            with self.assertRaisesRegex(ValueError, 'Unsupported runtime section'):
                runtime.normalize_member(object_bytes(section=section, flags=flags))

    def test_symbols_accept_weak_and_reject_other_bindings_common_and_bad_names(self):
        original = object_bytes(['entry'])
        weak = bytearray(original)
        struct.pack_into('<B', weak, 84, 0x22)   # inline C++ functions and template statics
        self.assertIn('entry', runtime.symbols(weak)[0])
        for at, fmt, value in [(84, 'B', 0x32), (86, 'H', 0xfff2), (72, 'I', 0xffffffff)]:
            data = bytearray(original)
            struct.pack_into('<' + fmt, data, at, value)
            with self.assertRaises(ValueError):
                runtime.symbols(data)

    def test_relocations_validate_targets_symbols_kinds_and_bounds(self):
        self.assertEqual(runtime.normalize_member(relocated()), relocated())
        for arguments in [{'offset': 1}, {'offset': 4}, {'kind': 5}, {'symbol': 9},
                          {'target': 9}, {'link': 3}, {'kind': 10}]:
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                runtime.normalize_member(relocated(**arguments))
        discarded = runtime.normalize_member(relocated(target=4))
        self.assertEqual(elf_sections(discarded)[5][1][1], 1)
        self.assertEqual(elf_sections(discarded)[5][1][5], 0)


if __name__ == '__main__':
    unittest.main()
