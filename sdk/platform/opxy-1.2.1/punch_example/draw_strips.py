"""Draws the example punch-in effects' flip-dot strips (40 x 18 frames side by side).

    python sdk/platform/opxy-1.2.1/punch_example/draw_strips.py

Writes the PNGs beside this file; tooling/punch_animation.py turns them into
animations.hpp. Hand-drawn strips (pump, ratchet, echo, tape stop, velocity)
are left alone.
"""
import math
import struct
import zlib
from pathlib import Path

W, H = 40, 18
HERE = Path(__file__).parent


class Frame:
    def __init__(self):
        self.dots = [[0] * W for _ in range(H)]

    def dot(self, x, y):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < W and 0 <= y < H:
            self.dots[y][x] = 1

    def line(self, x0, y0, x1, y1):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / max(n, 1)
            self.dot(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t)

    def rect(self, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.dot(x, y)

    def ellipse(self, cx, cy, rx, ry):
        for i in range(64):
            a = 2 * math.pi * i / 64
            self.dot(cx + rx * math.cos(a), cy + ry * math.sin(a))

    def text(self, x, y, s):
        glyphs = {'I': ['111', '010', '010', '010', '111'], 'M': ['10001', '11011', '10101', '10001', '10001']}
        for ch in s:
            g = glyphs[ch]
            for gy, row in enumerate(g):
                for gx, c in enumerate(row):
                    if c == '1':
                        self.dot(x + gx, y + gy)
            x += len(g[0]) + 1


def save(name, frames):
    width = W * len(frames)
    raw = b''.join(b'\0' + bytes(255 if frames[x // W].dots[y][x % W] else 0 for x in range(width)) for y in range(H))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, H, 8, 0, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
    (HERE / f'{name}.png').write_bytes(png)


def strip(count, draw):
    frames = []
    for f in range(count):
        frame = Frame()
        draw(frame, f, f / count)
        frames.append(frame)
    return frames


def faders(fr, f, t):
    for i in range(5):
        h = 2 + 12 * (0.5 - 0.5 * math.cos(2 * math.pi * (t + i * 0.12)))
        x = 7 + 6 * i
        fr.rect(x, int(16 - h), x + 2, 16)


# The filters' strips are still frames, one per encoder position: frame 0 the
# press (lowpass dark, hipass thin) to frame 16 open. The effect shows the
# frame for its position, so the screen moves only when the encoder does.
FILTER_FRAMES = 17


def filter_curve(fr, t, low, thick=False, label=False):
    # The passband filled solid under the response line (no resonance bump).
    s = t * FILTER_FRAMES / (FILTER_FRAMES - 1)   # 0 at the press .. 1 open
    c = 6 + 28 * s if low else 34 - 28 * s
    top, bottom = 4, 16
    for x in range(1, 39):
        if low:
            y = top if x <= c else top + (x - c) * (bottom - top) / (bottom - top)
        else:
            y = top if x >= c else top + (c - x)
        if y <= bottom:
            fr.rect(x, int(round(y)), x, bottom)


def band(fr, f, t):
    # A narrow peak filled solid on a flat floor; frame 0 low, the middle frame
    # centred, the last high.
    c = 5 + 30 * f / (FILTER_FRAMES - 1)
    for x in range(1, 39):
        d = abs(x - c)
        y = 4 if d <= 1 else 4 + (d - 1) * 12 / 4 if d <= 5 else 16
        fr.rect(x, int(round(min(16, y))), x, 16)


def chevrons(fr, f, t, label):
    fr.text(1, 7, label)
    for lane, y in enumerate((4, 9, 14)):
        for k in range(3):
            x = 12 + ((f * 2 + k * 9 + lane * 3) % 27)
            fr.line(x, y - 2, x + 2, y)
            fr.line(x + 2, y, x, y + 2)


def squash(fr, f, t):
    k = int(10 * (0.5 - 0.5 * math.cos(2 * math.pi * t)))
    fr.rect(3 + k, 2, 4 + k, 15)
    fr.rect(35 - k, 2, 36 - k, 15)
    span = (35 - k) - (5 + k)
    for i in range(span):
        x = 5 + k + i
        fr.dot(x, 9 + (3 if i % 4 < 2 else -3) * min(1, span / 20))


def sweep(fr, f, t):
    # A lava lamp: blobs of wax swell up out of a pool, drift to the top and sink
    # again, stretching and merging where they meet (metaballs; every blob makes
    # whole cycles over the strip, so it loops).
    tau = 2 * math.pi
    blobs = [(20 + 4 * math.sin(tau * t), 21 - 17 * (0.5 - 0.5 * math.cos(tau * t)), 3.2),
             (11 + 2 * math.sin(tau * 2 * t + 1), 19 - 15 * (0.5 - 0.5 * math.cos(tau * (t + 0.35))), 2.4),
             (29 + 2 * math.sin(tau * 2 * t + 3), 19 - 14 * (0.5 - 0.5 * math.cos(tau * (t + 0.7))), 2.6),
             (20, 20.5, 7.5)]   # the pool at the bottom
    for y in range(H):
        for x in range(W):
            field = sum(r * r / ((x - bx) ** 2 + 2.2 * (y - by) ** 2 + 0.01) for bx, by, r in blobs)
            if field >= 1:
                fr.dot(x, y)


def kill(fr, f, t):
    # A row of bars (the notes); a dashed knife comes down and chops all but the
    # lowest bar (the kick), which stays; then the bars grow back with the bar.
    heights = (11, 7, 13, 5, 10, 14, 6, 9)
    kick = 3
    knife = (f - 4) * 17 // 9 if 4 <= f <= 13 else None   # y 0 .. 17
    for i, h in enumerate(heights):
        x = 3 + 4 * i
        top = 16 - h
        if i != kick:
            if f > 17:
                top = 16 - h * (f - 17) // 6
            elif f > 13:
                continue
            elif knife is not None:
                top = max(top, knife + 1)
                if top > 16:
                    continue
        fr.rect(x, top, x + 2, 16)
    if knife is not None and knife <= 16:
        for x in range(0, 40, 2):
            fr.dot(x, knife)


def clip(fr, f, t):
    # A sine swelling past two ceiling lines and flattening against them.
    for x in range(0, 40, 3):
        fr.dot(x, 2)
        fr.dot(x, 15)
    a = 3 + 12 * (0.5 - 0.5 * math.cos(2 * math.pi * t))
    last = None
    for x in range(40):
        y = min(15, max(2, 8.5 - a * math.sin(2 * math.pi * (x - 5) / 20)))
        if last is not None:
            fr.line(x - 1, last, x, y)
        last = y


def gain(fr, f, t):
    # A sine swelling taller, with an arrow pointing up beside it.
    a = 2 + 6 * (0.5 - 0.5 * math.cos(2 * math.pi * t))
    last = None
    for x in range(5, 40):
        y = 9 - a * math.sin(2 * math.pi * x / 12)
        if last is not None:
            fr.line(x - 1, last, x, y)
        last = y
    fr.line(1, 16, 1, 3)
    fr.line(1, 3, 0, 5)
    fr.line(1, 3, 2, 5)


def gate(fr, f, t):
    period = 8
    for x in range(40):
        high = ((x + f) // (period // 2)) % 2 == 0
        fr.dot(x, 4 if high else 13)
        if (x + f) % (period // 2) == 0:
            fr.line(x, 4, x, 13)


def snare_roll(fr, f, t):
    x, gap = 38 - (f * 3) % 12, 12.0
    while x > 0:
        fr.dot(int(x), 9)
        x -= gap
        gap = max(2.0, gap * 0.7)


def wah(fr, f, t):
    h = 1 + 6 * (0.5 - 0.5 * math.cos(2 * math.pi * t))
    fr.ellipse(20, 9, 12, h)
    fr.line(9, 9, 31, 9) if h < 1.5 else None


def accent(fr, f, t):
    # Bars with the beats tall and the notes between them shrunk to ghosts; a
    # marker runs along the top.
    for i in range(8):
        h = 13 if i % 4 == 0 else 4
        x = 3 + 4 * i
        fr.rect(x, 16 - h, x + 2, 16)
    fr.rect(3 + 4 * (f % 8), 0, 5 + 4 * (f % 8), 0)


def octave(fr, f, t):
    # A wave that doubles its pitch: its cycles squeeze twice as close, and back.
    k = 1 + (0.5 - 0.5 * math.cos(2 * math.pi * t))
    last = None
    for x in range(40):
        y = 9 - 6 * math.sin(2 * math.pi * k * x / 20)
        if last is not None:
            fr.line(x - 1, last, x, y)
        last = y


PAGE2_STEPS = 20   # frames per pass


def page2(fr, f, t):
    # Page 2's idle screen, after stock's punch-in screen: a single dot travels
    # right to left and hops over the middle.
    step = f
    x = 36 - step * 2
    lift = max(0.0, 1 - abs(x - 18) / 8)   # 0 outside the middle, 1 at its top
    y = 8 - round(6 * (1 - (1 - lift) ** 2))   # middle height, peaking two dots from the top
    fr.dot(x, y)


def main():
    save('volume', strip(24, faders))
    save('lowpass', strip(FILTER_FRAMES, lambda fr, f, t: filter_curve(fr, t, True)))
    save('hipass', strip(FILTER_FRAMES, lambda fr, f, t: filter_curve(fr, t, False)))
    save('bandpass', strip(FILTER_FRAMES, band))
    save('boost_fx1', strip(24, lambda fr, f, t: chevrons(fr, f, t, 'I')))
    save('boost_fx2', strip(24, lambda fr, f, t: chevrons(fr, f, t, 'II')))
    save('squash', strip(24, squash))
    save('sweep', strip(32, sweep))
    save('kill', strip(32, kill))
    save('clip', strip(24, clip))
    save('gain', strip(24, gain))
    save('gate', strip(8, gate))
    save('snare_roll', strip(12, snare_roll))
    save('wah', strip(16, wah))
    save('accent', strip(16, accent))
    save('octave', strip(24, octave))
    save('page2', strip(PAGE2_STEPS, page2))


if __name__ == '__main__':
    main()
