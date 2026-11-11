"""Wave and ball: a wavetable as a row of solid isometric bars, dark to light, whose heights
hold the wave, with a light ball riding on top."""
import math
from tools.screen_kit.kit import *

NAME = 'Wave and ball'
LABELS = ['warp', 'stretch', 'position', 'drift']
HERO = (.85, .2, .1, .35)


def draw(a, b, c, d):
    cx, cy = 208, 68
    N, P_, LEN, B = 7, 24, 70, 26
    o = []
    amp = lerp(0, 20, a)
    wl = lerp(4, 12, b)
    ph = lerp(0, 2 * math.pi, c)
    ramp = [D2, D3, D4, D5, D6, D7, L]
    Z = lambda i: amp * math.sin(2 * math.pi * i / wl + ph)
    P = lambda x, y, z: iso(cx, cy, x, y, z)
    for i in range(N):
        if i == N - 1:
            o += box(cx, cy, i * P_, 0, Z(i) - 60, P_, LEN, 60, ramp[i])
            continue
        # two nodes a bar: the next bar hides this one's far side, so the whole silhouette is
        # drawn in the side grey and the lit top laid over it
        T = ramp[i]
        x, z0, z1 = i * P_, Z(i) - 60, Z(i)
        o.append(path([P(x, 0, z1), P(x + P_, 0, z1), P(x + P_, 0, z0), P(x + P_, LEN, z0), P(x, LEN, z0), P(x, LEN, z1)], darker(T), K, .5))
        o.append(path([P(x, 0, z1), P(x + P_, 0, z1), P(x + P_, LEN, z1), P(x, LEN, z1)], T, K, .5))
    # a black mask under the base, so the bars rise and fall by moving, never reshaping
    m = [P(0, LEN, -B), P(N * P_, LEN, -B), P(N * P_, 0, -B), (cx + N * P_ * .866 + 2, 225), (cx - LEN * .866 - 2, 225)]
    o.append(path(m, K))
    o.append(path([P(0, LEN, -B), P(N * P_, LEN, -B), P(N * P_, 0, -B), P(N * P_, 0, -B - .6), P(N * P_, LEN, -B - .6), P(0, LEN, -B - .6)], D4))
    q = d * (N - 1)
    k = int(min(q, N - 2))
    zb = lerp(Z(k), Z(k + 1), q - k)
    bp = P(q * P_ + P_ / 2, LEN / 2, zb)
    o.append(circle(bp[0], bp[1] - 10, 10, L, K, .5))
    return o
