"""Truchet: a glitch oscillator as a field of quarter-arc tiles edge to edge under the header,
in four column groups. Each control turns more of its group's tiles, so rows of waves break
into loops. Needs "sdk": "1.8" (320 nodes)."""
import math
from tools.screen_kit.kit import *

NAME = 'Truchet'
LABELS = ['warp', 'crush', 'drift', 'noise']
HERO = (.2, .5, .7, .35)
SDK = '1.8'


def draw(a, b, c, d):
    o = []
    T, COLS, ROWS = 25, 20, 8
    x0, y0 = 240 - COLS * T / 2, 20
    for j in range(ROWS):
        for i in range(COLS):
            q = i * 4 // COLS
            flip = cell_random(i, j, 7) < (a, b, c, d)[q]        # each tile has its own fixed threshold
            x, y = x0 + i * T, y0 + j * T
            g, w = CODE3[q], 2.4                                 # lines only: encoder 1 in 323232
            if flip:
                o.append(quarter_arc(x + T, y, T / 2, math.pi / 2, w, g))
                o.append(quarter_arc(x, y + T, T / 2, -math.pi / 2, w, g))
            else:
                o.append(quarter_arc(x, y, T / 2, 0, w, g))
                o.append(quarter_arc(x + T, y + T, T / 2, math.pi, w, g))
    return o
