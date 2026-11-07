"""The track filter (M3) on synthesis kit voices (drum_filter.hpp): the stock routines, data and
call sequence the adapter reproduces, replayed from the stock bytes, and the adapter's wiring.
The adapter's behaviour against host doubles is in test_drum_voices."""
import os
from pathlib import Path
import unittest
import json

from tooling.firmware import decode_branch
from tooling.formats import ldr, mapped_offset, sha

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
PROFILE = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))


def halves(data):
    return [int.from_bytes(data[i:i + 2], 'little') for i in range(0, len(data), 2)]


class DrumFilter(unittest.TestCase):
    def test_the_adapter_runs_stocks_filter_with_no_hook_of_its_own(self):
        adapter = (PLATFORM / 'drum_voice.cpp.in').read_text(encoding='utf-8')
        header = (PLATFORM / 'drum_filter.hpp').read_text(encoding='utf-8')
        for address in ('0x11b01dc8', '0x84042700', '0x11903500', '0x8d505220'):
            self.assertEqual(adapter.count('(%s)' % address), 1, address)
        # The key (11a0980a..11a09836), the headroom (11a0979c), the methods (+0x8, +0x14), the pool.
        for text in ('note * 0x00ffffffu', '0x007fffff', 'kDrumFilterHeadroom = 3', 'kDrumFilterMono = 2',
                     'kDrumFilterResetMethod = 5', 'kDrumFilterPool = 128', 'kDrumFilterLadder = 16'):
            self.assertIn(text, header)
        # Before the amplitude envelope, after the engine's render.
        render = adapter.index('@NAMESPACE@::render(slot->voice')
        self.assertLess(render, adapter.index('drum_filter_run('))
        self.assertLess(adapter.index('drum_filter_run('), adapter.index('output[i] = opxy::q31::multiply(output[i], amplitude[i])'))
        # The profiling workload runs the ladder on every voice.
        workload = (PLATFORM / 'benchmark_drum.cpp.in').read_text(encoding='utf-8')
        self.assertIn('platform::kDrumFilterLadder, 0x40000000', workload)
        # No profile patch belongs to the filter: it needs no stock hook.
        self.assertFalse([p for p in PROFILE['patches'] if 'filter' in p.get('symbol', '') and 'drum' in p.get('symbol', '')])

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_runs_a_synth_voices_filter_as_the_adapter_runs_a_kits(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), PROFILE['stock_sha256'])
        image = ldr(stock)

        def at(address, length):
            offset = mapped_offset(image, address, length)
            return stock[offset:offset + length]

        # Blackfin+ `reg = imm32` (0xdaNN, NN 0..7 R0..R7, 8..13 P0..P5) and CALL.A (0xde00).
        def load(address):
            w = halves(at(address, 8))
            self.assertEqual(w[0] & 0xff00, 0xda00, hex(address))
            return w[0] & 0xff, w[1] << 16 | w[2]

        def call_a(address):
            w = halves(at(address, 8))
            self.assertEqual((w[0], w[3]), (0xde00, 0), hex(address))
            return w[1] << 16 | w[2]

        # The normal renderer 11a09308: the track record from 8d518d94(0x84eab39c, B[voice+0x55])
        # into R4 (11a0933e..11a0935a).
        self.assertEqual(load(0x11a0933e), (0, 0x84eab39c))
        self.assertEqual(at(0x11a0934e, 4).hex(), '89e45500')   # R1 = B[P1 + 0x55]
        self.assertEqual(call_a(0x11a09352), 0x8d518d94)
        # The envelope's last value this block: R6 = [0x11903500 + 0xfc] (11a0960a, 11a09612).
        self.assertEqual(load(0x11a0960a), (9, 0x11903500))
        self.assertEqual(at(0x11a09612, 4).hex(), '0ee43f00')
        # The headroom: the machine's output shifted right by 3 (11a097a0).
        self.assertEqual(at(0x11a097a0, 4).hex(), '82cee803')
        # The filter: on/off a byte of 0x11b01dc8 by track, type a word of 0x84042700
        # (11a097d0..11a097f2), against the type the voice holds (+0x48, 11a097f4).
        self.assertEqual(load(0x11a097d0), (0, 0x11b01dc8))
        self.assertEqual(load(0x11a097da), (8, 0x84042700))
        self.assertEqual(at(0x11a097e2, 4).hex(), '8be45500')   # R3 = B[P1 + 0x55]: the track
        self.assertEqual(at(0x11a097ea, 2).hex(), '0899')       # R0 = B[P1]: on/off
        self.assertEqual(at(0x11a097f2, 2).hex(), '485e')       # P1 = P0 + (P1 << 2): the type's word
        self.assertEqual(at(0x11a097f4, 4).hex(), '11e41200')   # R1 = [P2 + 0x48]
        # The call: key = B[voice+0x50] x 0xffffff + 0x7fffff, saturated, the object
        # [voice+0x44], the block 0x11b01948, the record R4, then +0xc (stereo) or +0x8 (mono).
        self.assertEqual(load(0x11a0980a), (3, 0x00ffffff))
        self.assertEqual(load(0x11a09812), (1, 0x11b01948))
        self.assertEqual(at(0x11a0981a, 4).hex(), '08e41100')   # R0 = [P1 + 0x44]
        self.assertEqual(at(0x11a09828, 4).hex(), '82e45000')   # R2 = B[P0 + 0x50]
        self.assertEqual(at(0x11a0982c, 2).hex(), 'da40')       # R2 *= R3
        self.assertEqual(load(0x11a0982e), (3, 0x007fffff))
        self.assertEqual(at(0x11a09836, 4).hex(), '04c41326')   # R3 = R2 + R3 (S)
        self.assertEqual(at(0x11a0983c, 8).hex(), '82ce04c4c9ac36b1')   # R2 = R4 || P1 = [P1 + 0xc] || [SP + 0x10] = R6
        self.assertEqual(at(0x11a09850, 8).hex(), '82ce04c489ac36b1')   # R2 = R4 || P1 = [P1 + 0x8] || [SP + 0x10] = R6
        self.assertEqual(at(0x11a09844, 2).hex(), 'f3b0')       # [SP + 0xc] = R3: the key
        # A type change (11a09e44..11a09e78): the type into +0x48, 8d505220(type,
        # &voice+0x44, B[voice], track), then the object's +0x14 with R1 = 0.
        self.assertEqual(at(0x11a09e4a, 4).hex(), '10e61200')   # [P2 + 0x48] = R0
        self.assertEqual(at(0x11a09e50, 2).hex(), '0299')       # R2 = B[P0]: the voice index
        self.assertEqual(call_a(0x11a09e54), 0x8d505220)
        self.assertEqual(at(0x11a09e5c, 4).hex(), '04cc0942')   # R1 = R1 - R1
        self.assertEqual(at(0x11a09e74, 2).hex(), '49ad')       # P1 = [P1 + 0x14]
        # A normal voice's constructor builds and resets it at every note (8d51b0d0..8d51b180).
        self.assertEqual(load(0x8d51b0d6), (8, 0x84042700))
        self.assertEqual(decode_branch(0x8d51b0f4, at(0x8d51b0f4, 4)), (0x8d505220, True))
        self.assertEqual(at(0x8d51b17e, 2).hex(), '49ad')       # P1 = [P1 + 0x14]

        # The factory 8d505220(type, dest, index, track): types 0..18, an entry of 0x30
        # bytes at 0x08039b2c, called with (index, track), the object stored at dest.
        self.assertEqual(at(0x8d50522c, 2).hex(), '9f60')       # R7 = 0x13
        self.assertEqual(load(0x8d505234), (8, 0x08039b2c))
        self.assertEqual(at(0x8d505232, 2).hex(), '8761')       # R7 = 0x30
        self.assertEqual(at(0x8d505256, 2).hex(), '2893')       # [P5] = R0
        # Registrations (8d506bd4..8d506c3e): svf 10, the ladder 16, z lowpass 9, z hipass
        # 17 with their factories; 18 (dynamic) with none.
        self.assertEqual(at(0x8d506bd4, 2).hex(), '5060')       # R0 = 0xa
        self.assertEqual(load(0x8d506bec), (2, 0x8d51c128))
        self.assertEqual(at(0x8d506bf4, 2).hex(), '8060')       # R0 = 0x10
        self.assertEqual(load(0x8d506c0c), (2, 0x8d51bd6c))
        self.assertEqual(at(0x8d506c14, 2).hex(), '4860')       # R0 = 9
        self.assertEqual(load(0x8d506c24), (2, 0x8d51be30))
        self.assertEqual(at(0x8d506c2c, 2).hex(), '8860')       # R0 = 0x11
        self.assertEqual(at(0x8d506c3c, 2).hex(), '9060')       # R0 = 0x12, with R2 = 0
        # Each factory constructs in the pool slot 0x84c6239c + index x 0x90, refusing 128 up.
        for factory in (0x8d51c058, 0x8d51c128, 0x8d51bd6c, 0x8d51be30):
            code = at(factory, 0x70)
            self.assertIn((0x84c6239c).to_bytes(4, 'little')[2:] + (0x84c6239c).to_bytes(4, 'little')[:2], code, hex(factory))
        # Their method tables: +0x8 process (mono) and +0x14 reset are stock routines.
        for table in (0x8405302c, 0x84053054, 0x8405307c, 0x840530a4):
            methods = [int.from_bytes(at(table + 4 * i, 4), 'little') for i in range(6)]
            self.assertTrue(all(0x8d500000 <= m < 0x8d520000 for m in methods), hex(table))
        # z lowpass's process 8d51bc44 reads cutoff, resonance, envelope and key amount
        # (words 16..19, +0x40..+0x4c of the record in R2) with the key and envelope
        # from the stack, as the adapter passes them.
        self.assertEqual(int.from_bytes(at(0x8405302c + 8, 4), 'little'), 0x8d51bc44)
        self.assertEqual(at(0x8d51bc44, 2).hex(), '0a32')       # P1 = R2: the record
        self.assertEqual(at(0x8d51bc56, 4).hex(), '0fe41300')   # R7 = [P1 + 0x4c]
        self.assertEqual(at(0x8d51bc68, 4).hex(), '0fe41000')   # R7 = [P1 + 0x40]

        # The drum renderer 8d51b274 already computes each kit voice's filter envelope
        # from words 32..35 (8d51b36e..8d51b38a) into the same buffer (8d51b3a6), then
        # calls the machine's +0xc render with the block at 0x11b01948 (8d51b54c..8d51b55a).
        self.assertEqual(at(0x8d51b380, 4).hex(), '29e42000')   # R1 = [P5 + 0x80]: word 32
        self.assertEqual(at(0x8d51b36e, 4).hex(), '2ee42300')   # R6 = [P5 + 0x8c]: word 35
        self.assertEqual(decode_branch(0x8d51b38a, at(0x8d51b38a, 4)), (0x8d50e358, True))
        self.assertEqual(load(0x8d51b3a6), (8, 0x11903500))
        self.assertEqual(load(0x8d51b54c), (1, 0x11b01948))
        self.assertEqual(at(0x8d51b558, 2).hex(), 'c0ac')       # P0 = [P0 + 0xc]
        # A kit voice's constructor path (8d51aee2..) never writes +0x44 or +0x48: a
        # kit voice has no filter object of stock's.
        kit = at(0x8d51aee2, 0x8d51b06e - 0x8d51aee2)
        for store in ('20e61100', '20e61200'):   # [P4 + 0x44] = R0, [P4 + 0x48] = R0
            self.assertNotIn(bytes.fromhex(store), kit)


if __name__ == '__main__':
    unittest.main()
