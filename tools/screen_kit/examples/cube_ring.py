"""Cube ring: an FM pair as a ring of small isometric cubes. A lit, lifted cube runs round it
with a tail fading to black behind it."""
import math
from tools.screen_kit.kit import *

NAME = 'Cube ring'
LABELS = ['ratio', 'index', 'detune', 'feedback']
HERO = (.75, .9, .5, .5)


def draw(a, b, c, d):
    cx, cy = 240, 128.5
    o = []
    N, S, Rr = 14, 17, 74
    ramp = [L, D7, D6, D5, D4, D3, D2, D2]
    head = int(round(a * (N - 1)))
    tail = 1 + round(b * 6)
    opp = (head + N // 2 + round(lerp(-3, 3, c))) % N
    lift = lerp(0, 12, d)
    order = sorted(range(N), key=lambda i: math.cos(2 * math.pi * i / N) + math.sin(2 * math.pi * i / N))   # far cubes first
    for i in order:
        q = 2 * math.pi * i / N
        x = Rr * math.cos(q) - S / 2
        y = Rr * math.sin(q) - S / 2
        dist = (head - i) % N
        dist2 = (opp - i) % N
        best = min(dist if dist < tail else 99, (dist2 + 1) * 1 + 1 if dist2 < tail // 2 else 99)
        T = ramp[min(best, 7)] if best < 99 else D2
        o += box(cx, cy, x, y, lift if i == head else 0, S, S, S, T)
    return o
