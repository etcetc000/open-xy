"""Sliding slabs: a layered oscillator. Four thin slabs with rounded corners, darkest on top,
each slid out along one axis by the level of its layer."""
from tools.screen_kit.kit import *

NAME = 'Sliding slabs'
LABELS = ['sub', 'body', 'shine', 'noise']
HERO = (.2, .9, .4, .7)


def draw(a, b, c, d):
    cx, cy = 212, 110
    W, D, H, G = 120, 70, 10, 5
    o = []
    for i in (3, 2, 1, 0):                                        # back to front: lowest slab first
        s = lerp(-18, 18, (a, b, c, d)[i])
        o += slab(cx, cy, s, 0, (3 - i) * (H + G), W, D, H, CODE3[i], 7, K)
    return o
