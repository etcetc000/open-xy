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


if __name__ == '__main__':
    unittest.main()
