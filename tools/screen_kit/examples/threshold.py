"""Threshold: noise as four tiles of one ordered-dither pattern, hollow then dark to light, each
filled to its control's level. A note could lift a tile's level for a moment."""
from tools.screen_kit.kit import *

NAME = 'Threshold'
LABELS = ['density', 'grit', 'color', 'haze']
HERO = (.2, .45, .7, .9)
SDK = '1.8'


def draw(a, b, c, d):
    o = []
    C, G = 11, 1
    for t, v in enumerate((a, b, c, d)):
        x0, y0 = 16 + 120 * t, 74
        for j in range(8):
            for i in range(8):
                x, y = x0 + i * C, y0 + j * C
                if BAYER[j][i] < v * 64:
                    o.append(rect(x, y, C - G, C - G, K, L, .8) if t == 0 else rect(x, y, C - G, C - G, CODE[t]))
                else:
                    o.append(rect(x, y, C - G, C - G, K))
    return o
