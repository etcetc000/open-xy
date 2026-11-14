"""Blinds: a formant bank as one blind of eight thick slats in four pairs, dark to light. Each
control tilts its pair open, so the slats thin and more of the sun behind shows through."""
import math
from tools.screen_kit.kit import *

NAME = 'Blinds'
LABELS = ['low', 'mid', 'high', 'air']
HERO = (.2, .75, .45, 1)


def sun(sx, sy, m):
    """A disc in four rings that thin and dim outward, brighter as the blinds open (m 0..1).
    Each note would push every ring one step outward."""
    lv = round(m * 6)
    o = []
    for k in range(4):
        f = k / 4
        o.append(circle(sx, sy, 30 + 34 * f, 'none', RAMP[max(1, round(1 + lv - 3 * f))], 3.2 - 2 * f))
    return o + [circle(sx, sy, 26, RAMP[2 + lv])]


def draw(a, b, c, d):
    o = []
    x0, w, top, pitch, H = 160, 160, 62, 17, 13
    o.append(rrect(x0 - 6, top - 12, w + 12, 8, 3, D4))          # the rail
    cx = 396                                                      # a white pull cord hanging from above
    o.append(rect(cx - .75, 20, 1.5, 92, L))
    o.append(rrect(cx - 7, 110, 14, 30, 6.5, L))
    sx, sy = x0 + w / 2, top + 3.5 * pitch + H / 2
    o += sun(sx, sy, ((a + b + c + d) / 4) ** 2)                  # curved: dim until the blinds are well open
    xm, e = x0 + w / 2, .01                                       # each pair is one outline: two slats joined by a
    for i, v in enumerate((a, b, c, d)):                          # 0.02 px slit, so only four outlines change shape
        h = max(1.6, H * math.cos(lerp(0, 1.45, v)))
        A, B = [rrect_pts(x0, top + k * pitch + (H - h) / 2, w, h, min(3, h / 2 - .1)) for k in (2 * i, 2 * i + 1)]
        ya, yb = A[7][1], B[0][1]
        o.append(path(A[:8] + [(xm + e, ya), (xm + e, yb)] + B + [(xm - e, yb), (xm - e, ya)] + A[8:], CODE3[i]))  # in its knob grey
    return o
