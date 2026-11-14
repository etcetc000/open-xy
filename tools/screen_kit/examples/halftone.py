"""Halftone: a subtractive synth as a field of tiny dim dots that swell into large light ones
inside a soft round spot. Env sets how big the middle dots get, key moves the spot, cutoff
sets its size and res sharpens its edge. Four dot sizes, so each dot only scales."""
import math
from tools.screen_kit.kit import *

NAME = 'Halftone'
LABELS = ['env', 'key', 'cutoff', 'res']
HERO = (.35, .55, .45, .4)


def draw(a, b, c, d):
    o = []
    P = 22
    COLS, ROWS = 20, 8
    x0, y0 = 240 - (COLS - 1) * P / 2, 120 - (ROWS - 1) * P / 2
    LV = (1, 3, 5.5, 8.5)
    sx, sy = lerp(80, 400, b), 120
    R = lerp(40, 110, c)
    soft = lerp(10, 50, d)
    peak = lerp(.4, 1, a)
    for j in range(ROWS):
        for i in range(COLS):
            x, y = x0 + i * P, y0 + j * P
            g = max(0, min(1, (R - math.hypot(x - sx, y - sy)) / soft + .5)) * peak
            k = min(len(LV) - 1, int(g * len(LV)))
            o.append(circle(x, y, LV[k], L) if k else circle(x, y, 1, D2))
    return o
