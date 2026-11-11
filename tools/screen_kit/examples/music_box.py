"""Music box: a bell-like synth as a hand-cranked music box, a pinned cylinder in four bands,
dark to light, with a comb in front and a crank at the end. More pins, more of that control.
The cylinder and crank turning with notes is a stated bend of move-or-copy."""
import math
from tools.screen_kit.kit import *

NAME = 'Music box'
LABELS = ['ratio', 'index', 'tone', 'damp']
HERO = (.25, .55, .45, 1)


def draw(a, b, c, d):
    cx, cy = 208, 76
    P = lambda x, y, z=0: iso(cx, cy, x, y, z)
    T, CY, CZ, R = 8, 36, 30, 20
    X0, X1 = 26, 146
    o = box(cx, cy, 0, 0, 0, 178, 104, T, D2)                    # the base

    def band(x1, x2):
        s = [math.radians(-45 + 180 * k / 14) for k in range(15)]
        return [P(x1, CY + R * math.cos(q), CZ + R * math.sin(q)) for q in s] + [P(x2, CY + R * math.cos(q), CZ + R * math.sin(q)) for q in reversed(s)]
    w = (X1 - X0) / 4
    for i in range(4):
        x1 = X0 + i * w
        o.append(path(band(x1, x1 + w), CODE3[i], K, .5))
    # the end cap, as a right face would be
    o.append(path([P(X1, CY + R * math.cos(2 * math.pi * k / 24), CZ + R * math.sin(2 * math.pi * k / 24)) for k in range(24)], D7, K, .5))
    grid = [(.2, 10), (.55, 62), (.8, 30), (.35, 95), (.15, 45), (.65, 112), (.45, 20), (.85, 80)]
    for i, v in enumerate((a, b, c, d)):
        count = 1 + round(v * 7)
        for j, (u, deg) in enumerate(grid):
            if j < count:
                q = math.radians(deg)
                p = P(X0 + (i + u) * w, CY + R * math.cos(q), CZ + R * math.sin(q))
                o.append(circle(p[0], p[1], 2.5, L if i == 0 else K))
            else:
                o.append(PARKED)                                 # pins not showing wait off screen
    # the comb: fourteen teeth, long to short, on a plate in front of the cylinder
    Z = T + 5
    tip = CY + R + 3
    pts = []
    N = 14
    tw = (X1 - X0) / N
    for j in range(N):
        x = X0 + j * tw
        root = lerp(96, 80, j / (N - 1))
        pts += [P(x, root, Z), P(x, tip, Z), P(x + tw * .6, tip, Z), P(x + tw * .6, root, Z)]
    pts += [P(X1, 80, Z), P(X1, 100, Z), P(X0, 100, Z)]
    o.append(path(pts, D5, K, .5))
    # stand, axle and crank at the near end
    o += box(cx, cy, 156, CY - 6, T, 5, 12, CZ - T, D4)
    a1, a2 = P(X1, CY, CZ), P(172, CY, CZ)
    o.append(outlined(line(a1[0], a1[1], a2[0], a2[1], 3, D6)))
    a3 = P(172, CY, CZ + 26)
    o.append(outlined(line(a2[0], a2[1], a3[0], a3[1], 3, D6)))
    o.append(circle(a3[0], a3[1], 6, L, K, .5))
    return o
