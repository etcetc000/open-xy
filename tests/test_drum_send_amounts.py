"""Per-key sends at 1% steps (SDK 1.15): the gain table, the stored bytes and their
coarse codes, the preset carry and the reviewed sites. Pure Python: the C++ side runs
in tests/fixtures/drum_send_mixer.cpp, drum_locks.cpp and drumline.cpp."""
import json
import re
from pathlib import Path
import unittest

from tooling.firmware import decode_branch

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
HEADER = (PLATFORM / 'drum_send.hpp').read_text(encoding='utf-8')
PROFILE = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))
MAX = 2 ** 31 - 1


def multiply(a, b):
    """q31::multiply, stock's (T) form: the product floored, MIN * MIN saturated."""
    product = a * b
    return MAX if product >= 2 ** 62 else product >> 31


def add(a, b):
    return max(-2 ** 31, min(MAX, a + b))


def curve(send, k):
    """Stock's send gain for knob k, before the level: FX I 0.8 k^2 + 0.2 k
    (8d510d7a..8d510daa, 0x66666666 and 0x19999999), FX II k (8d510e0a)."""
    return k if send else add(multiply(multiply(k, k), 0x66666666), multiply(k, 0x19999999))


def amount_position(n):
    return MAX if n >= 100 else (n * 2 ** 31 + 50) // 100


# The old codes' knob positions, (c - 1) / 6 (SDK 1.10..1.14).
OLD_POSITIONS = [0, 0, 357913941, 715827883, 1073741824, 1431655765, 1789569707, MAX]


def table():
    body = re.search(r'kDrumSendLevels\[kDrumSends\]\[kDrumSendIndexMask \+ 1\] = \{(.*?)\};', HEADER, re.S).group(1)
    rows = re.findall(r'\{([^{}]*)\}', body)
    return [[int(v) for v in row.replace('\n', ' ').split(',') if v.strip()] for row in rows]


def shadow(amount):
    """drum_send_shadow: the nearest old code, as the header computes it without a divide."""
    return 1 + (((6 * min(amount, 100) + 50) * 1311) >> 17)


def stored(byte, code):
    """drum_send_stored: an amount when the byte is one and the code its shadow, else the old code."""
    if 1 <= byte <= 101 and code == shadow(byte - 1):
        return byte
    return 101 + code if code else 0


class SendAmounts(unittest.TestCase):
    def test_every_index_sends_what_stocks_knob_sends(self):
        rows = table()
        self.assertEqual([len(r) for r in rows], [128, 128])
        for send in (0, 1):
            want = [0] + [curve(send, amount_position(n)) for n in range(101)]
            want += [curve(send, OLD_POSITIONS[c]) for c in range(1, 8)]
            want += [0] * (128 - len(want))
            self.assertEqual(rows[send], want, send)
        # The old codes keep SDK 1.10's gains exactly (its kDrumSendGains).
        self.assertEqual(rows[0][102:109], [0, 119304646, 334053011, 644245093, 1049880892, 1550960410, 2147483644])
        self.assertEqual(rows[1][102:109], [0, 357913941, 715827883, 1073741824, 1431655765, 1789569707, MAX])
        # At 0, 50 and 100 an old code and the amount it shows send the same.
        for send in (0, 1):
            self.assertEqual((rows[send][1], rows[send][51], rows[send][101]), (rows[send][102], rows[send][105], rows[send][108]))

    def test_the_gui_arithmetic_needs_no_divide(self):
        # The header's multiply-and-shift forms, exact over their ranges.
        self.assertIn('* 1311) >> 17', HEADER)
        self.assertIn('* 10923) >> 16', HEADER)
        for amount in range(101):
            self.assertEqual(shadow(amount), 1 + (6 * amount + 50) // 100)
        self.assertEqual([((c * 100 + 3) * 10923) >> 16 for c in range(7)], [0, 17, 33, 50, 67, 83, 100])
        self.assertIn('(n * 205) >> 11', (PLATFORM / 'drumline.cpp').read_text(encoding='utf-8'))
        self.assertTrue(all((n * 205) >> 11 == n // 10 for n in range(100)))
        # Every amount keeps a code 1..7 (never TRK's 0), so a byte with code 0 is stale.
        self.assertEqual(sorted({shadow(a) for a in range(101)}), list(range(1, 8)))

    def test_a_key_keeps_its_amount_and_old_keys_play_as_before(self):
        for amount in range(101):
            self.assertEqual(stored(amount + 1, shadow(amount)), amount + 1)
        # Bytes stock left 0: the old code (0 TRK), as SDK 1.10..1.14 played it.
        for code in range(8):
            self.assertEqual(stored(0, code), 101 + code if code else 0)
        # A byte its code doesn't match (older firmware turned the code since, or a
        # sampler's play mode pasted onto the pad): the code wins.
        for byte in range(256):
            self.assertEqual(stored(byte, 0), 0)
        self.assertEqual(stored(38, 4), 105)

    def test_presets_carry_the_amounts_above_bit_16_of_random(self):
        # Stock's writer exports random as the pad word >>> 16 (8d4e3ece); its import
        # stores max(random << 16, 0) (8d4e3558): bits 16 and up never reach the word.
        def stock_import(random):
            word = (random << 16) & 0xffffffff
            return 0 if word >= 2 ** 31 else word
        for word in (0x4d4b, 0x01ab4d4b, 0x7fff4d4b, (5 << 25 | 6 << 28) | 0x4d4b):
            for fx1, fx2 in ((0, 0), (1, 101), (38, 0), (101, 101), (0, 51)):
                random = (word >> 16) | fx1 << 16 | fx2 << 23
                self.assertTrue(0 <= random < 2 ** 31)
                self.assertEqual(stock_import(random), word & 0xffff0000)
                self.assertEqual(((random >> 16) & 0x7f, (random >> 23) & 0x7f), (fx1, fx2))
        source = (PLATFORM / 'drum_synthesis_stock.cpp').read_text(encoding='utf-8')
        self.assertIn('index << (kDrumSendIndexBits * send)', source)
        self.assertIn('(number >> (16 + kDrumSendIndexBits * send)) & kDrumSendIndexMask', source)

    def test_the_preset_sites_call_randoms_converters(self):
        hooks = {p['address']: p for p in PROFILE['patches'] if p['kind'] == 'hook'}
        for site, symbol, converter, base in (('0x8d4e73ac', 'extensions_drum_export_random', 0x8d4e3ece, 'P4'),
                                              ('0x8d4e3b9a', 'extensions_drum_import_random', 0x8d4e3558, 'P5')):
            patch = hooks[site]
            self.assertEqual((patch['symbol'], patch['service'], patch['length'], patch['call']), (symbol, 'drums', 4, True))
            self.assertEqual(decode_branch(int(site, 16), bytes.fromhex(patch['before'])), (converter, True))
            stub = re.search(symbol + r':\n(.*?)\n\.size', (PLATFORM / 'drum_hooks.s').read_text(encoding='utf-8'), re.S).group(1)
            self.assertEqual([line.strip() for line in stub.splitlines()],
                             [f'R1 = {base};', 'JUMP.L ' + symbol.replace('extensions_', 'sdk_') + ';'])

    def test_the_byte_offsets_are_free_on_drumline(self):
        # +0x19 and +0x1b: past the type byte (+0x18), beside the key (+0x1a), below
        # tune (+0x1c), level (+0x1d) and pan (+0x1e), inside the 32-byte record pad
        # copy and the project writer move whole.
        self.assertIn('const u32 kDrumSendByte[kDrumSends] = {0x19, 0x1b};', HEADER)
        storage = (PLATFORM / 'drum_storage.hpp').read_text(encoding='utf-8')
        self.assertIn('kDrumPan = 0x1e, kDrumLevel = 0x1d', storage)


if __name__ == '__main__':
    unittest.main()
