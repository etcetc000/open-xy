"""Checkerboard: a chord synth as a flat isometric checked board with four round pieces, dark
to light, each on its own lane, moved a square at a time."""
import math
from tools.screen_kit.kit import *

NAME = 'Checkerboard'
LABELS = ['invert', 'spread', 'color', 'add']
HERO = (.3, .7, .15, .55)


def draw(a, b, c, d):
    cx, cy = 197, 50
    S, X, Y = 25, 8, 4
    o = plate(cx, cy, X * S, Y * S, 5, D2, D1)
    for i in range(X):
        for j in range(Y):
            o.append(path([iso(cx, cy, i * S, j * S), iso(cx, cy, (i + 1) * S, j * S), iso(cx, cy, (i + 1) * S, (j + 1) * S),
                           iso(cx, cy, i * S, (j + 1) * S)], D3 if (i + j) % 2 else D1, K, .5))
    for j, v in enumerate((a, b, c, d)):
        k = round(v * (X - 1))
        p = iso(cx, cy, (k + .5) * S, (j + .5) * S)
        rx, ry, h = 14, 8, 8
        T = CODE3[j]
        side = ([(p[0] - rx, p[1] - h)] + [(p[0] + rx * math.cos(math.pi - math.pi * m / 12), p[1] + ry * math.sin(math.pi - math.pi * m / 12)) for m in range(13)]
                + [(p[0] + rx, p[1] - h)])
        o.append(path(side, darker(T), K, .5))
        o.append(ellipse(p[0], p[1] - h, rx, ry, T, K, .5, k=24))
    return o
