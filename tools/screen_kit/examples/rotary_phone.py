"""Rotary phone: a stepped wavetable as a rounded, light silhouette of a rotary phone, small
dots in place of numbers, a handset on its cradle and a coiled cord. No grey code: every
control is read from movement alone."""
import math
from tools.screen_kit.kit import *

NAME = 'Rotary phone'
LABELS = ['position', 'table', 'warp', 'drift']
HERO = (.5, .5, .5, .5)


def draw(a, b, c, d):
    cx, cy = 240, 143
    lift, tilt = lerp(0, 13, c), lerp(0, -8, c)
    handset = [(165, 54), (315, 54), (315, 58), (322, 58), (322, 72), (288, 72), (288, 64), (192, 64), (192, 72), (158, 72), (158, 58), (165, 58)]
    o = [path(round_poly([(190, 84), (290, 84), (318, 208), (162, 208)], 7), L),                                   # body
         path(round_poly([(208, 74), (213, 74), (213, 79), (267, 79), (267, 74), (272, 74), (272, 86), (208, 86)], 1.5, 2), L),  # cradle
         path(turned(round_poly(handset, 2.5, 2), 240, 62, tilt, 0, -lift), L)]                                    # handset, lifted by warp
    # the coiled cord: loops along a curve from the handset down to the body, bowed by drift
    top = turned([(166, 72)], 240, 62, tilt, 0, -lift)[0]
    bot = (165, 192)
    bow = lerp(14, 58, d)

    def centre(t):
        return (lerp(top[0], bot[0], t) - bow * math.sin(math.pi * t), lerp(top[1], bot[1], t))
    LOOPS, R, LEAD = 12, 4.2, .05
    pts = []
    for i in range(LOOPS * 10 + 1):
        u = i / (LOOPS * 10)
        t = LEAD + (1 - 2 * LEAD) * u
        (x0, y0), (x1, y1) = centre(t - .002), centre(t + .002)
        tx, ty = x1 - x0, y1 - y0
        m = math.hypot(tx, ty)
        tx, ty = tx / m, ty / m
        th = 2 * math.pi * LOOPS * u
        cx_, cy_ = centre(t)
        pts.append((cx_ + tx * R * math.cos(th) - ty * R * math.sin(th) - tx * R, cy_ + ty * R * math.cos(th) + tx * R * math.sin(th) - ty * R))
    line_ = [centre(0), centre(LEAD / 2)] + pts + [centre(1 - LEAD / 2), centre(1)]          # 125 points
    o += [ribbon(line_[k:k + 32], 1.3, L) for k in (0, 31, 62, 93)]  # four pieces of 32 points: four reshaping outlines
    # the dial: dots for the numbers, a black gap, the light face, ten finger holes
    angs = [-60 + 30 * i for i in range(10)]
    for i in range(10):
        ax = math.radians(angs[i])
        o.append(circle(cx + 58 * math.cos(ax), cy + 58 * math.sin(ax), 1.8, K))
    o += [circle(cx, cy, 47, K), circle(cx, cy, 45.6, L)]
    spin = lerp(0, 28, a)
    pick = round(b * 9)
    for i in range(10):
        ax = math.radians(angs[i] + spin)
        o.append(circle(cx + 34 * math.cos(ax), cy + 34 * math.sin(ax), 7.5, D5 if i == pick else K))
    o += [circle(cx, cy, 14, K), circle(cx, cy, 10, L)]
    return o
