"""Spokes: a detuned pair as two fans of thin spokes in one round window, one dim and one light.
Detune pulls the hubs apart so the overlap bends into curved bands; at all 0 it is one clean
wheel. Each half fan is a fixed outline that only moves and turns; black side pieces trim
it to the window."""
import math
from tools.screen_kit.kit import *

NAME = 'Spokes'
LABELS = ['detune', 'phase', 'shape', 'stereo']
HERO = (.55, .3, .65, .4)


def draw(a, b, c, d):
    o = []
    cx, cy, R, SEP, N, tipw = 240, 125, 68, 34, 30, 1.1
    Rt = R + SEP / 2 + 2

    def half(hx, hy, rot, fill, h):
        pts = []
        step = 2 * math.pi / N
        dt = tipw / 2 / Rt
        for k in range(h * N // 2, (h + 1) * N // 2):
            t = rot + step * k
            pts.append((hx + 2.5 * math.cos(t - step / 2), hy + 2.5 * math.sin(t - step / 2)))
            pts.append((hx + Rt * math.cos(t - dt), hy + Rt * math.sin(t - dt)))
            pts.append((hx + Rt * math.cos(t + dt), hy + Rt * math.sin(t + dt)))
        return path(pts, fill)
    ax, ay = cx - lerp(0, SEP / 2, a), cy + lerp(0, 20, c)
    bx, by = cx + lerp(0, SEP / 2, a), cy
    rot = math.radians(lerp(0, 12, b))
    thick = math.radians(lerp(0, 1.1, d))
    o += [half(ax, ay, 0, D5, h) for h in (0, 1)]
    o += [half(bx, by, rot, L, h) for h in (0, 1)]
    o += [half(bx, by, rot + thick, L, h) for h in (0, 1)]       # stereo: a second light fan turned a little
    o += [circle(ax, ay, 3, K), circle(bx, by, 3, K)]
    arc = lambda s_: [(cx + s_ * R * math.sin(math.pi * i / 24), cy - R * math.cos(math.pi * i / 24)) for i in range(25)]
    o.append(path([(cx + 1, 20), (0, 20), (0, 220), (cx + 1, 220)] + arc(-1)[::-1], K))
    o.append(path([(cx - 1, 20), (480, 20), (480, 220), (cx - 1, 220)] + arc(1)[::-1], K))
    o.append(circle(cx, cy, R + .5, 'none', D2, 1))
    return o
