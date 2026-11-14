"""Turned form: an additive synth as a turned vase in stacked hairline ellipses, dim at the
bottom and light at the top, on a thin dark plate. The four controls set its profile."""
from tools.screen_kit.kit import *

NAME = 'Turned form'
LABELS = ['low', 'body', 'depth', 'air']
HERO = (.35, 1, .2, .7)


def draw(a, b, c, d):
    cx, base, W = 240, 170, 100
    o = box(cx, base - 8 - W / 2, 0, 0, 0, W, W, 6, D2)          # the plate never moves
    N = 14
    ctrl = [lerp(10, 44, v) for v in (a, b, c, d)]
    ramp = [D3, D3, D4, D4, D5, D5, D5, D6, D6, D6, D7, D7, L, L]
    for i in range(N):
        t = i / (N - 1) * 3
        k = min(int(t), 2)
        f = t - k
        f = f * f * (3 - 2 * f)
        R = lerp(ctrl[k], ctrl[k + 1], f)
        y = base - 14 - i * 6                                    # the lowest ring sits on the plate
        rx, ry = iso_axes(R)                                     # flat hairline rings may change size
        o.append(ellipse(cx, y, rx, ry, 'none', ramp[i], 1, k=28))
    return o
