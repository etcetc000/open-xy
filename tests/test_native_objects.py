"""Exported objects: what both linkers receive after normalization."""
import struct
import unittest

from tooling.formats import elf_sections
from tooling.native import normalize_object

ALLOC, MERGE_STRINGS, GROUP = 0x2, 0x30, 0x200


def obj(sections, symbols):
    """A Blackfin ET_REL object: (name, type, flags, data) sections after the null one,
    then .symtab with (binding, section) symbols, then .shstrtab."""
    names = b'\0'
    def name(text):
        nonlocal names
        at = len(names); names += text.encode() + b'\0'; return at
    table = b'\0' * 16 + b''.join(struct.pack('<IIIBBH', 0, 0, 0, binding << 4 | 2, 0, section)
                                  for binding, section in symbols)
    entries = [(name(n), kind, flags, data, 0, 0) for n, kind, flags, data in sections]
    symtab = len(entries) + 1
    entries.append((name('.symtab'), 2, 0, table, symtab + 1, 1))
    entries.append((name('.shstrtab'), 3, 0, None, 0, 0))
    body, headers = bytearray(), [b'\0' * 40]
    for n, kind, flags, data, link, info in entries:
        data = names if data is None else data
        headers.append(struct.pack('<10I', n, kind, flags, 0, 52 + len(body), len(data), link, info, 1,
                                   16 if kind == 2 else 0))
        body += data
    header = bytearray(52)
    header[:6] = b'\x7fELF\x01\x01'
    struct.pack_into('<HHI', header, 16, 1, 106, 1)
    struct.pack_into('<I', header, 32, 52 + len(body))
    struct.pack_into('<HHHHH', header, 42, 40, 0, 40, len(headers), len(headers) - 1)
    return bytes(header + body + b''.join(headers))


class Normalize(unittest.TestCase):
    def test_comdat_groups_become_plain_sections_and_global_definitions(self):
        # An out-of-line inline function: .group naming it, its .text.* member and a weak symbol.
        data = obj([('.group', 17, 0, struct.pack('<II', 1, 2)),
                    ('.text._Z6renderv', 1, ALLOC | 0x4 | GROUP, b'\0\0'),
                    ('.rodata.str', 1, ALLOC | MERGE_STRINGS, b'a\0'),
                    ('.comment', 1, MERGE_STRINGS, b'GCC\0')],
                   [(2, 2), (2, 0), (1, 3)])
        out = normalize_object(data)
        headers = [h for _at, h in elf_sections(out)]
        self.assertEqual(headers[1][1:3], (0, 0), 'the group header is inactive (SHT_NULL)')
        self.assertEqual(headers[2][2], ALLOC | 0x4)
        self.assertEqual(headers[3][2], ALLOC)
        self.assertEqual(headers[4][2], MERGE_STRINGS, 'unallocated sections keep their pooling flags')
        symtab = headers[5][4]
        bindings = [out[symtab + 16 * i + 12] >> 4 for i in range(1, 4)]
        self.assertEqual(bindings, [1, 2, 1], 'defined weak symbols become global; weak imports stay weak')
        # Contents and everything else in each header are untouched.
        self.assertEqual(len(out), len(data))
        for (_at, before), after in zip(elf_sections(data), headers):
            self.assertEqual(before[3:], after[3:])
        self.assertEqual(normalize_object(out), out)

    def test_multiple_groups_leave_no_linkable_group_records(self):
        data = obj([('.group', 17, 0, struct.pack('<II', 1, 3)),
                    ('.group', 17, 0, struct.pack('<II', 1, 4)),
                    ('.text.first', 1, ALLOC | 0x4 | GROUP, b'\x01\x02'),
                    ('.text.second', 1, ALLOC | 0x4 | GROUP, b'\x03\x04')],
                   [(2, 3), (2, 4)])
        out = normalize_object(data)
        before = [h for _at, h in elf_sections(data)]
        after = [h for _at, h in elf_sections(out)]
        # Both GNU and the browser must ignore every obsolete group record;
        # making them PROGBITS instead produces duplicate .group output sections.
        self.assertEqual([h[1:3] for h in after[1:3]], [(0, 0), (0, 0)])
        self.assertEqual([h[2] for h in after[3:5]], [ALLOC | 0x4] * 2)
        self.assertEqual(len(out), len(data))
        for original, normalized in zip(before, after):
            self.assertEqual(original[3:], normalized[3:])
        table = after[5][4]
        self.assertEqual([struct.unpack_from('<H', out, table + 16 * i + 14)[0]
                          for i in (1, 2)], [3, 4], 'symbol section indices stay fixed')
        self.assertEqual(normalize_object(out), out)


if __name__ == '__main__':
    unittest.main()
