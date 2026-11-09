"""Audio-callback call graph: recursion fails, stack bounds follow the deepest path."""
from types import SimpleNamespace
import unittest

from tooling.native import audio_stack_bounds

BASE = 0x1000


def disassembly(functions):
    lines = []
    address = BASE + 0x100
    for name, body in functions:
        lines.append(f'{address:08x} <{name}>:')
        for text in body:
            lines.append(f'{address:08x}:\t00 00 \t{text}')
            address += 2
    return '\n'.join(lines)


def image(entry, slot=8):
    module = bytearray(0x100)
    module[slot:slot + 4] = entry.to_bytes(4, 'little')
    return bytes(module)


SYMBOLS = {'_m_vtable': dict(value=BASE, size=44, type=1)}
MACHINE = [SimpleNamespace(id='m', kind='synth', prefix='m')]


class AudioStack(unittest.TestCase):
    def test_bound_is_the_deepest_direct_path(self):
        text = disassembly([('render', ['LINK 0x10;', f'CALL 0x{BASE + 0x108:x} <leaf>;', 'UNLINK;', 'RTS;']),
                            ('leaf', ['[--SP] = R7;', 'R7 = [SP++];', 'RTS;'])])
        self.assertEqual(audio_stack_bounds(text, image(BASE + 0x100), BASE, SYMBOLS, MACHINE), {'m': 0x18 + 4})

    def test_recursion_fails(self):
        text = disassembly([('render', ['LINK 0x0;', f'CALL 0x{BASE + 0x100:x} <render>;', 'RTS;'])])
        with self.assertRaisesRegex(ValueError, 'Recursive'):
            audio_stack_bounds(text, image(BASE + 0x100), BASE, SYMBOLS, MACHINE)

    def test_drum_render_uses_alternate_callback(self):
        # Stock's drum path calls +12; +8 only clears an unused output buffer.
        text = disassembly([('normal', ['RTS;']),
                            ('drum', ['LINK 0x100;', 'UNLINK;', 'RTS;'])])
        module = bytearray(image(BASE + 0x100))
        module[12:16] = (BASE + 0x102).to_bytes(4, 'little')
        machines = [SimpleNamespace(id='m', kind='drum', prefix='m')]
        self.assertEqual(audio_stack_bounds(text, module, BASE, SYMBOLS, machines), {'m': 0x108})

    def test_drum_render_recursion_fails(self):
        text = disassembly([('render', [f'CALL 0x{BASE + 0x100:x} <render>;', 'RTS;'])])
        machines = [SimpleNamespace(id='m', kind='drum', prefix='m')]
        with self.assertRaisesRegex(ValueError, 'Recursive'):
            audio_stack_bounds(text, image(BASE + 0x100, 12), BASE, SYMBOLS, machines)

    def test_native_routine_counts_its_declared_stack(self):
        # render calls a machine's native routine: its manifest's stackBytes count,
        # its computed jumps stay inside it, and its measured frame must fit them.
        def listing(push):
            return disassembly([('render', ['LINK 0x10;', f'CALL 0x{BASE + 0x108:x} <routine>;', 'UNLINK;', 'RTS;']),
                                ('routine', [push, 'JUMP (P0);', '(R7:4) = [SP++];', 'RTS;']),
                                ('deep', ['LINK 0x400;', 'UNLINK;', 'RTS;'])])
        module = bytearray(image(BASE + 0x100))
        module[0x40:0x44] = (BASE + 0x110).to_bytes(4, 'little')  # deep's address in data: an indirect target
        native = {BASE + 0x108: ('m/native/routine', 64)}
        text = listing('[--SP] = (R7:4);')
        self.assertEqual(audio_stack_bounds(text, module, BASE, SYMBOLS, MACHINE, native), {'m': 0x18 + 64})
        # Without its manifest, the jump would reach every function in data.
        self.assertEqual(audio_stack_bounds(text, module, BASE, SYMBOLS, MACHINE), {'m': 0x18 + 16 + 0x408})
        with self.assertRaisesRegex(ValueError, 'takes 72 bytes of stack, more than its stackBytes 64'):
            audio_stack_bounds(listing('LINK 0x40;'), module, BASE, SYMBOLS, MACHINE, native)
        with self.assertRaisesRegex(ValueError, 'indirect CALL'):
            audio_stack_bounds(listing('CALL (P1);'), module, BASE, SYMBOLS, MACHINE, native)


if __name__ == '__main__':
    unittest.main()
