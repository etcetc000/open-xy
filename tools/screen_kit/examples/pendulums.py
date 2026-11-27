"""Pendulums: a pad as four half discs on rods hanging from the header bar, hollow then dark to
light. Each control lengthens its rod; a note would swing them, the longer ones slower. The
rods are the screen's four reshaping outlines; the domes and dots only move."""
import math
from tools.screen_kit.kit import *

NAME = 'Pendulums'
LABELS = ['bass', 'tenor', 'alto', 'soprano']
HERO = (.2, .75, .35, .85)


def draw(a, b, c, d):
    o = []
    for i, v in enumerate((a, b, c, d)):
        x = 60 + 120 * i
        ln = lerp(56, 186, v)
        R = 24
        deg = 0                                                  # the swing angle a note would set

        def rot(px, py):
            q = math.radians(deg)
            return (x + px * math.cos(q) + py * math.sin(q), 20 - px * math.sin(q) + py * math.cos(q))
        r1, r2 = rot(0, 0), rot(0, ln - R)
        o.append(line(r1[0], r1[1], r2[0], r2[1], 1, D5))
        dot = rot(0, ln + 3)
        o.append(circle(dot[0], dot[1], 6, D7))
        dome = [rot(R * math.cos(math.radians(180 + 180 * k / 20)), ln + R * math.sin(math.radians(180 + 180 * k / 20))) for k in range(21)]
        o.append(path(dome, K, L, 1) if i == 0 else path(dome, CODE[i]))
    return o
