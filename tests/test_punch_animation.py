"""Punch-in flip-dot animations: PNG strips to frame data, and the punch-in hook sites."""
import struct
import unittest
import zlib

from tooling import punch_animation
from tooling.pipeline import ROOT, config

EXAMPLES = ROOT / 'sdk/platform/opxy-1.2.1/punch_example'
STRIPS = (('rise_dots', 'octave_up.png'), ('fall_dots', 'octave_down.png'),
          ('stutter_dots', 'stutter.png'), ('hold_dots', 'hold.png'))


def png(width, height, color, channels, pixel, filter_kind=0):
    """A minimal 8-bit PNG whose every row uses one filter type."""
    rows = []
    for y in range(height):
        line = bytearray()
        for x in range(width):
            line += bytes(pixel(x, y))
        if filter_kind == 1:  # Sub
            encoded = bytearray(line)
            for i in range(len(line) - 1, channels - 1, -1):
                encoded[i] = (line[i] - line[i - channels]) & 255
            line = encoded
        rows.append(bytes([filter_kind]) + bytes(line))
    chunk = lambda kind, body: (struct.pack('>I', len(body)) + kind + body +
                                struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, color, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))


class PunchAnimation(unittest.TestCase):
    def test_lit_pixels_set_bits_row_by_row(self):
        lit = {(0, 0), (39, 0), (5, 3), (39, 17)}
        data = png(80, 18, 0, 1, lambda x, y: [255 if (x % 40, y) in lit and x < 40 else 0], filter_kind=1)
        first, second = punch_animation.frames(data)
        dots = {(i % 40, i // 40) for i in range(720) if first[i >> 3] >> (i & 7) & 1}
        self.assertEqual(dots, lit)
        self.assertEqual(second, bytes(90))

    def test_rgba_uses_brightness_and_alpha(self):
        data = png(40, 18, 6, 4, lambda x, y: [255, 255, 255, 255] if x == 1 else
                   [255, 255, 255, 0] if x == 2 else [0, 0, 0, 255])
        (frame,) = punch_animation.frames(data)
        self.assertEqual([i for i in range(720) if frame[i >> 3] >> (i & 7) & 1], [1 + 40 * y for y in range(18)])

    def test_rejects_wrong_sizes(self):
        for width, height in ((41, 18), (40, 17), (0, 18)):
            with self.assertRaises(ValueError):
                punch_animation.frames(png(width, height, 0, 1, lambda x, y: [0]))

    def test_example_header_matches_its_strips(self):
        strips = [(name, (EXAMPLES / file).read_bytes()) for name, file in STRIPS]
        self.assertEqual((EXAMPLES / 'animations.hpp').read_text(encoding='ascii'),
                         punch_animation.header(strips))

    def test_hooks_are_authenticated_calls(self):
        hooks = {p['symbol']: p for p in config()['patches'] if p.get('symbol', '').startswith('sdk_punch_')}
        self.assertEqual(set(hooks), {'sdk_punch_mask', 'sdk_punch_note', 'sdk_punch_track',
                                      'sdk_punch_step', 'sdk_punch_animation'})
        self.assertTrue(all(p['kind'] == 'hook' and p['call'] for p in hooks.values()))


if __name__ == '__main__':
    unittest.main()
