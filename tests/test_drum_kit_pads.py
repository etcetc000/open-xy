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

    def test_the_held_pad_page_and_reset_are_stocks_calls(self):
        # 8c0b2068's query of the track's engine, and M4's call of Drumline's pad reset.
        for site, symbol, target in (('0x8c0b20a2', 'sdk_drum_held_pad_engine', 0x8d58781c),
                                     ('0x8c0b32d6', 'sdk_drum_pad_reset', 0x8d582048)):
            patch = hook(site)
            self.assertEqual((patch['symbol'], patch['service'], patch['length'], patch['call']), (symbol, 'drums', 8, True))
            self.assertEqual(absolute_target(bytes.fromhex(patch['before'])), target)
        # M1's own test stays stock's: an owned kit passes it by answering as Drumline.
        self.assertFalse(any(p['address'] == '0x8c0b309c' for p in PROFILE['patches']))
        self.assertNotIn('extensions_drum_pad_action', HOOKS)

    def test_the_builder_pins_the_reviewed_profile(self):
        import hashlib
        digest = hashlib.sha256((ROOT / 'profiles/opxy-1.2.1.json').read_bytes()).hexdigest()
        self.assertIn(f"export const PROFILE_SHA256 = '{digest}';", (ROOT / 'builder/core.mjs').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
