"""Washing line: the line is the string, plucked. A hollow sock, a dark shirt and a mid-grey
towel hang on it, held up by a light forked prop."""
import math
from tools.screen_kit.kit import *

NAME = 'Washing line'
LABELS = ['pluck', 'damp', 'body', 'tension']
HERO = (.5, .5, .5, .5)


def draw(a, b, c, d):
    ptop = lerp(96, 60, d)
    sag = lerp(34, 5, d)
    dip = lerp(0, 26, b)

    def y(x):
        t = x / 400
        return lerp(52, ptop, t) + 4 * sag * t * (1 - t) + dip * math.exp(-((x - 205) / 62) ** 2)
    o = [line(0, 208, 480, 208, 1, D2)]
    k = list(range(0, 401, 50))
    for i in range(8):                         # the line in eight straight pieces, so none reshapes much
        o.append(line(k[i], y(k[i]), k[i + 1], y(k[i + 1]), 1.4, D7))
    o.append(line(400, ptop, 480, 52, 1.4, D7))

    def peg(x, f):
        return rect(x - 1.5, y(x) - 5, 3, 11, f)
    # encoder 1: the hollow sock slides along the line
    sx = lerp(38, 128, a)
    sy = y(sx) + 4
    sock = ((-10, 0), (10, 0), (10, 32), (28, 42), (28, 56), (2, 56), (-10, 44))
    o += [path([(sx + px, sy + py) for px, py in sock], K, L, 1), peg(sx, L)]
    # encoder 2: the heavier shirt pulls the line down
    u = lerp(.9, 1.3, b)
    hy = y(205) + 3
    shirt = ((-13, 0), (-5, 0), (0, 6), (5, 0), (13, 0), (36, 18), (26, 30), (16, 22), (16, 70), (-16, 70), (-16, 22), (-26, 30), (-36, 18))
    o += [path([(205 + px * u, hy + py * u) for px, py in shirt], D3), peg(205, D3)]
    # encoder 3: the towel gets longer
    ty = y(310) + 3
    th = lerp(40, 118, c)
    o += [rect(285, ty, 50, th, D6), line(285, ty + th - 12, 335, ty + th - 12, 1.4, K),
          line(285, ty + th - 7, 335, ty + th - 7, 1.4, K), peg(298, D6), peg(322, D6)]
    # encoder 4: the prop lifts the line, which tightens it
    o += [rect(397.5, ptop + 4, 5, 240, L),
          path([(391, ptop - 7), (395, ptop - 7), (400, ptop), (405, ptop - 7), (409, ptop - 7), (402.5, ptop + 6), (397.5, ptop + 6)], L)]
    return o
