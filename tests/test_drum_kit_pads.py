"""A drum kit's pads as stock Drumline's: Shift and a pad hold it for per-key editing,
and a held pad scrambles alone. Pure Python: the sites the profile reviews and the
stub's returns; tests/fixtures/drum_gui.cpp runs the C++ on the host in CI."""
import json
from pathlib import Path
import re
import struct
import unittest

from tooling.firmware import absolute_target

ROOT = Path(__file__).resolve().parents[1]
PROFILE = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))
HOOKS = (ROOT / 'sdk/platform/opxy-1.2.1/drum_hooks.s').read_text(encoding='utf-8')


def hook(address):
    return next(p for p in PROFILE['patches'] if p['kind'] == 'hook' and p['address'] == address)


class KitPads(unittest.TestCase):
    def test_the_two_calls_are_stocks_pad_check_and_one_pad_scramble(self):
        for site, symbol, target in (('0x8c0b2978', 'sdk_drum_shift_pad', 0x8d5b8814),
                                     ('0x8c0b3170', 'sdk_drum_pad_scramble', 0x8d4d60f4)):
            patch = hook(site)
            self.assertEqual((patch['symbol'], patch['service'], patch['length'], patch['call']), (symbol, 'drums', 8, True))
            self.assertEqual(absolute_target(bytes.fromhex(patch['before'])), target)

    def test_the_held_pad_action_replays_drumlines_test(self):
        patch = hook('0x8c0b309c')
        self.assertEqual((patch['symbol'], patch['service'], patch['length'], patch['call']),
                         ('extensions_drum_pad_action', 'drums', 12, True))
        # R0 = 4; CC = R7 == R0; IF !CC JUMP +0xa; R0 = B[P5 + 0x2c] (Z); CC = R0 == 0
        self.assertEqual(struct.unpack('<6H', bytes.fromhex(patch['before'])),
                         (0x6020, 0x0807, 0x1005, 0xe4a8, 0x002c, 0x0c00))
        # The absolute call returns to 8c0b30a4; stock's jump goes to 8c0b30aa (8c0b30a0
        # + 2 x 5) and its scramble at 8c0b3168. The stub returns there by those offsets.
        returns = 0x8c0b309c + 8
        self.assertEqual(0x8c0b30a0 + 2 * 5 - returns, 6)
        self.assertEqual(0x8c0b3168 - returns, 0xc4)
        stub = re.search(r'extensions_drum_pad_action:\n(.*?)\n\.size', HOOKS, re.S).group(1)
        self.assertIn('R1 = 0xc4 (X);', stub)
        self.assertIn('R0 += 6;', stub)
        self.assertIn('R0 = B[P5 + 0x2c] (Z);\n    CC = R0 == 0;\n    RTS;', stub)

    def test_the_builder_pins_the_reviewed_profile(self):
        import hashlib
        digest = hashlib.sha256((ROOT / 'profiles/opxy-1.2.1.json').read_bytes()).hexdigest()
        self.assertIn(f"export const PROFILE_SHA256 = '{digest}';", (ROOT / 'builder/core.mjs').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
