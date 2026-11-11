"""Split: a granular synth as a square divided again and again into quarters, shown only as
scattered tiles of different sizes, hollow then dark to light, one quarter per control."""
from tools.screen_kit.kit import *

NAME = 'Split'
LABELS = ['grain', 'spray', 'smear', 'shimmer']
HERO = (.35, .6, .85, .5)
POOL = {76: 1, 38: 4, 19: 16, 9.5: 22}                           # most tiles of each size one quarter can show
PARK_AT = (640, 320)                                              # off screen


def draw(a, b, c, d):
    o = []
    S, X0, Y0, MOST, rad = 152, 240 - 152 / 2, 52, 22, 3.3
    for qi, v in enumerate((a, b, c, d)):
        qx, qy = X0 + (qi % 2) * S / 2, Y0 + (qi // 2) * S / 2
        leaves = []

        def go(x, y, w, depth, key):                             # split a cell while its fixed chance is under the control
            if depth < 3 and cell_random(qi, key, 1) < v * 1.15 - depth * .18:
                for k, (dx, dy) in enumerate(((0, 0), (w / 2, 0), (0, w / 2), (w / 2, w / 2))):
                    go(x + dx, y + dy, w / 2, depth + 1, key * 4 + k + 1)
            elif cell_random(qi, key, 2) < .5:
                leaves.append((x, y, w))
        go(qx, qy, S / 2, 0, 0)
        shown = leaves[:MOST]
        for size, count in POOL.items():                         # tiles never change size: each moves into place or parks
            mine = [(x, y) for x, y, w in shown if w == size]
            g = 2 if size > 20 else 1.5
            r = rad if size > 20 else min(rad, 1.5)
            for k in range(count):
                x, y = mine[k] if k < len(mine) else PARK_AT
                o.append(rrect(x + g, y + g, size - 2 * g, size - 2 * g, r, K, L, .8) if qi == 0
                         else rrect(x + g, y + g, size - 2 * g, size - 2 * g, r, CODE[qi]))
    return o
