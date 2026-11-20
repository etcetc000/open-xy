"""Barcode: a pulse oscillator as a light barcode in four sections between guard bars. Each
control turns more of its section's bars wide, and a dot under the section brightens from
black to white."""
from tools.screen_kit.kit import *

NAME = 'Barcode'
LABELS = ['pw', 'sub', 'ratio', 'noise']
HERO = (.25, .5, .19, .75)
TOP, TALL, SLOT = 58, 110, 5.5
PARK_AT = (640, 320)                                              # off screen


def draw(a, b, c, d):
    o = []
    W, G = 16 * SLOT, 9.5
    x0 = 240 - (4 * W + 3 * G) / 2
    xs = [x0 + r * (W + G) for r in range(4)]
    for x in [xs[0] - 7] + [x + W + 1.5 for x in xs]:            # guard bars in pairs, dropping below the bars
        o.append(rect(x, TOP, 1.4, TALL + 10, L))
        o.append(rect(x + 3, TOP, 1.4, TALL + 10, L))
    for r, v in enumerate((a, b, c, d)):
        hits = set(euclid_hits(round(v * 16)))
        for i in range(16):                                       # a thin and a wide copy of each bar: one shown, one parked,
            x, wide = xs[r] + i * SLOT + .4, i in hits             # so no outline changes shape
            o.append(rect(*PARK_AT, 1.1, TALL, L) if wide else rect(x, TOP, 1.1, TALL, L))
            o.append(rect(x, TOP, 3.4, TALL, L) if wide else rect(*PARK_AT, 3.4, TALL, L))
    for r, v in enumerate((a, b, c, d)):
        o.append(circle(xs[r] + 8 * SLOT, TOP + TALL + 18, 5, RAMP[round(v * 8)]))   # brightens, as hardsync's knob 3
    return o
