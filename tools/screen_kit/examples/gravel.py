"""Gravel: an unstable oscillator as a grid of hollow squares in four column groups, each
loosened by its control: every square turns and slides by its own fixed amount, so a control
only says how much."""
import math
from tools.screen_kit.kit import *

NAME = 'Gravel'
LABELS = ['drift', 'wobble', 'spread', 'slop']
HERO = (.1, .4, .7, 1)


def draw(a, b, c, d):
    o = []
    S, P = 18, 26
    COLS, ROWS = 12, 6
    x0, y0 = 240 - (COLS - 1) * P / 2, 120 - (ROWS - 1) * P / 2
    for j in range(ROWS):
        for i in range(COLS):
            q = i // 3
            v = (a, b, c, d)[q]
            ang = (cell_random(i, j, 3) - .5) * math.radians(80) * v
            dx, dy = (cell_random(i, j, 4) - .5) * 10 * v, (cell_random(i, j, 5) - .5) * 10 * v
            x, y = x0 + i * P + dx, y0 + j * P + dy
            h = S / 2
            pts = turned([(x - h, y - h), (x + h, y - h), (x + h, y + h), (x - h, y + h)], x, y, math.degrees(ang))
            o.append(path(pts, K, CODE3[q], 1.2))
    return o
