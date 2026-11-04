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


def load(register, address):
    return [f'{register}.H = 0x{address >> 16:x};', f'{register}.L = 0x{address & 0xffff:x};']


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

    def bound(self, *functions, module=None):
        text = disassembly(functions)
        return audio_stack_bounds(text, module or image(BASE + 0x100), BASE, SYMBOLS, MACHINE)['m']

    def test_register_call_reaches_the_address_it_builds(self):
        for register, transfer in (('P2', 'CALL (P2);'), ('FP', 'CALL (FP);'), ('P4', 'JUMP (P4);')):
            render = ['LINK 0x10;', *load(register, BASE + 0x10c), transfer, 'UNLINK;', 'RTS;']
            self.assertEqual(self.bound(('render', render), ('leaf', ['[--SP] = R7;', 'R7 = [SP++];', 'RTS;'])),
                             0x18 + 4, transfer)

    def test_address_built_in_a_data_register_counts(self):
        render = ['LINK 0x10;', *load('R2', BASE + 0x10e), 'P2 = R2;', 'CALL (P2);', 'UNLINK;', 'RTS;']
        self.assertEqual(self.bound(('render', render), ('leaf', ['LINK 0x20;', 'UNLINK;', 'RTS;'])), 0x18 + 0x28)

    def test_shared_high_half_pairs_with_every_low_half(self):
        # As compiled: the high half once, each case's low half just before its call.
        render = [f'P2.H = 0x{BASE >> 16:x};',
                  f'P2.L = 0x{BASE + 0x10e:x};', 'CALL (P2);', 'RTS;',
                  f'P2.L = 0x{BASE + 0x114:x};', 'CALL (P2);', 'RTS;']
        self.assertEqual(self.bound(('render', render), ('small', ['LINK 0x10;', 'UNLINK;', 'RTS;']),
                                    ('large', ['LINK 0x40;', 'UNLINK;', 'RTS;'])), 0x48)

    def test_a_built_address_counts_once_however_many_calls_use_it(self):
        render = ['LINK 0x10;', *load('P2', BASE + 0x10e), 'CALL (P2);', 'CALL (P2);', 'UNLINK;', 'RTS;']
        self.assertEqual(self.bound(('render', render), ('leaf', ['LINK 0x20;', 'UNLINK;', 'RTS;'])), 0x18 + 0x28)

    def test_table_dispatch_keeps_following_the_targets_register_calls(self):
        # The dispatch is expanded one level, but its target's built addresses are its own calls.
        render = ['LINK 0x10;', 'P0 = [P1];', 'CALL (P0);', 'UNLINK;', 'RTS;']
        target = ['LINK 0x20;', *load('P2', BASE + 0x116), 'CALL (P2);', 'UNLINK;', 'RTS;']
        module = bytearray(image(BASE + 0x100))
        module[0x40:0x44] = (BASE + 0x10a).to_bytes(4, 'little')
        self.assertEqual(self.bound(('render', render), ('target', target), ('leaf', ['LINK 0x40;', 'UNLINK;', 'RTS;']),
                                    module=bytes(module)), 0x18 + 0x28 + 0x48)

    def test_a_built_address_is_not_a_table_dispatch(self):
        table = bytearray(image(BASE + 0x100))
        table[0x40:0x44] = (BASE + 0x114).to_bytes(4, 'little')  # large, in another machine's table
        render = ['LINK 0x10;', *load('P2', BASE + 0x10e), 'R0 = 0x1 (X);', 'CALL (P2);', 'UNLINK;', 'RTS;']
        leaf = ('leaf', ['LINK 0x20;', 'UNLINK;', 'RTS;'])
        large = ('large', ['LINK 0x100;', 'UNLINK;', 'RTS;'])
        self.assertEqual(self.bound(('render', render), leaf, large, module=bytes(table)), 0x18 + 0x28)
        # Overwritten from memory, the same register may hold any table entry.
        render[3] = 'P2 = [P1];'
        self.assertEqual(self.bound(('render', render), leaf, large, module=bytes(table)), 0x18 + 0x108)

    def test_only_a_register_transfer_to_a_function_start_is_an_edge(self):
        leaf = ('leaf', ['LINK 0x20;', 'UNLINK;', 'RTS;'])
        data = ['LINK 0x10;', *load('P2', BASE + 0x40), 'CALL (P2);', 'UNLINK;', 'RTS;']
        self.assertEqual(self.bound(('render', data), leaf), 0x18)
        inside = ['LINK 0x10;', *load('P2', BASE + 0x110), 'CALL (P2);', 'UNLINK;', 'RTS;']
        self.assertEqual(self.bound(('render', inside), leaf), 0x18)
        no_transfer = ['LINK 0x10;', *load('R0', BASE + 0x10a), 'UNLINK;', 'RTS;']
        self.assertEqual(self.bound(('render', no_transfer), leaf), 0x18)

    def test_register_recursion_fails(self):
        with self.assertRaisesRegex(ValueError, 'Recursive'):
            self.bound(('render', ['LINK 0x0;', *load('P2', BASE + 0x100), 'CALL (P2);', 'RTS;']))
        first = ['LINK 0x0;', *load('FP', BASE + 0x10a), 'CALL (FP);', 'RTS;']
        second = [*load('P3', BASE + 0x100), 'JUMP (P3);']
        with self.assertRaisesRegex(ValueError, 'Recursive'):
            self.bound(('render', first), ('other', second))

    def test_register_jump_to_its_own_start_is_a_loop(self):
        render = ['LINK 0x10;', *load('P2', BASE + 0x100), 'JUMP (P2);']
        self.assertEqual(self.bound(('render', render)), 0x18)

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
