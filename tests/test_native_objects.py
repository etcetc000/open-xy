"""Exported objects: what both linkers receive after normalization."""
import struct
import unittest

from tooling.formats import elf_sections
from tooling.native import global_definitions, normalize_object, one_definition, readable_symbol

ALLOC, MERGE_STRINGS, GROUP = 0x2, 0x30, 0x200


def obj(sections, symbols):
    """A Blackfin ET_REL object: (name, type, flags, data) sections after the null one,
    then .symtab with (binding, section[, name[, type]]) symbols, functions unless
    typed, then .shstrtab, which also names the symbols."""
    names = b'\0'
    def name(text):
        nonlocal names
        at = len(names); names += text.encode() + b'\0'; return at
    def symbol(binding, section, symbol_name='', kind=2):
        return struct.pack('<IIIBBH', name(symbol_name) if symbol_name else 0, 0, 0, binding << 4 | kind, 0, section)
    table = b'\0' * 16 + b''.join(symbol(*s) for s in symbols)
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



ROTATION = '__ZN4opxy4draw8rotationEl'
TEXT = [('.text', 1, ALLOC | 0x4, b'\0\0')]


class OneDefinition(unittest.TestCase):
    """Two machines defining one global symbol can't share a firmware: the build says which."""

    def test_definitions_are_the_named_defined_global_and_weak_symbols(self):
        data = obj(TEXT, [(1, 1, '_render'), (2, 1, ROTATION), (1, 0, '_imported'), (0, 1, '_local'),
                          (1, 1, 'display.cpp', 4), (1, 1)])
        self.assertEqual(global_definitions(data), {'_render', ROTATION})

    def test_readable_names(self):
        self.assertEqual(readable_symbol(ROTATION), 'opxy::draw::rotation')
        self.assertEqual(readable_symbol('__ZN4opxy4draw11NoteTracker6updateERKNS0_5InputE'),
                         'opxy::draw::NoteTracker::update')
        self.assertEqual(readable_symbol('__Z4sinel'), 'sine')
        self.assertEqual(readable_symbol('_moth_register'), '_moth_register')

    def test_two_machines_defining_one_symbol_name_both(self):
        # An inline helper each kept out of line, made global by normalize_object.
        moth = normalize_object(obj(TEXT, [(2, 1, ROTATION), (1, 1, '_moth_register')]))
        mdkit = normalize_object(obj(TEXT, [(2, 1, ROTATION), (1, 1, '_mdkit_register')]))
        with self.assertRaises(ValueError) as caught:
            one_definition([('moth', moth), ('mdkit', mdkit)], {'moth': 'Moth', 'mdkit': 'MD KIT'})
        self.assertEqual(str(caught.exception), "Moth and MD KIT can't be in one OS: both define opxy::draw::rotation")

    def test_every_pair_of_machines_is_named(self):
        objects = [(m, obj(TEXT, [(1, 1, ROTATION)])) for m in ('a', 'b', 'c')]
        with self.assertRaises(ValueError) as caught:
            one_definition(objects, {'a': 'A', 'b': 'B', 'c': 'C'})
        self.assertEqual(str(caught.exception).splitlines(), [
            f"{x} and {y} can't be in one OS: both define opxy::draw::rotation" for x, y in (('A', 'B'), ('A', 'C'), ('B', 'C'))])

    def test_distinct_names_one_machine_and_the_sdk_pass(self):
        one_definition([('moth', obj(TEXT, [(1, 1, '_moth_register')])),
                        ('mdkit', obj(TEXT, [(1, 1, '_mdkit_register'), (0, 1, '_local')])),
                        # One machine's own objects, and the SDK's beside a machine, are for the linker to judge.
                        ('mdkit', obj(TEXT, [(1, 1, ROTATION)])), ('mdkit', obj(TEXT, [(1, 1, ROTATION)])),
                        (None, obj(TEXT, [(1, 1, '_moth_register')]))],
                       {'moth': 'Moth', 'mdkit': 'MD KIT'})


if __name__ == '__main__':
    unittest.main()
