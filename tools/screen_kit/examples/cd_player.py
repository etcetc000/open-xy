"""CD player: a lo-fi digital oscillator as an isometric CD deck with a shiny disc and a smeared
bow of light, and beside it a pair of headphones standing in the same projection, the wave
they play running between their pads, joined to the deck by a cord that drops off screen.
Freq swaps a mini disc for a full one and sets the wave's cycles, crush holds the wave in
coarse steps, scrub turns the bow of light and slides the wave, tone adds a harmonic. A
small LCD on the deck carries the grey code."""
import math
from tools.screen_kit.kit import *

NAME = 'CD player'
LABELS = ['freq', 'crush', 'scrub', 'tone']
HERO = (.7, .4, .3, .5)


def lcd(P, a, b, c, d):
    """A small LCD lying in the deck's front corner: a play mark, then one symbol per encoder in
    the code greys. Freq: a hollow disc, small or full. Crush: level bars. Scrub: a ring of
    dots, one lit, turning with the disc. Tone: a stair of harmonics."""
    o = []
    SC, MX, MY, AX = .62, 53, 110, 52                            # drawn at 62 %, centred on the deck
    P0 = P
    P = lambda x, y: P0(AX + (x - MX) * SC, 112 + (y - MY) * SC)
    pl = lambda pts: [P(x, y) for x, y in pts]
    o.append(path(pl([q for q, _ in rounded_plan(13, 100, 80, 20, 4, 3)]), K, D4, .6))   # black glass, thin bezel
    o.append(path(pl([(18, 106), (25, 110), (18, 114)]), D5))   # play mark
    r = 3 if a < .5 else 5                                       # freq: a hollow disc, mini or full
    o.append(path(pl([(36 + r * math.cos(t), 110 + r * math.sin(t)) for t in [2 * math.pi * k / 16 for k in range(16)]]), K, L, .6))
    n = 1 + round(2 * b)                                         # crush: up to three level bars
    for k in range(3):
        h = 4 + 3 * k
        x = 46 + 3.5 * k if k < n else 300
        o.append(path(pl([(x, 115), (x + 2.2, 115), (x + 2.2, 115 - h), (x, 115 - h)]), D3))
    th = math.radians(lerp(-90, 270, c))                         # scrub: six dots, one lit at the bow's angle
    for k in range(6):
        t = 2 * math.pi * k / 6
        o.append(circle(*P(66 + 4.5 * math.cos(t), 110 + 4.5 * math.sin(t)), .9 * SC + .1, D6))
    th = math.pi / 3 * round(th / (math.pi / 3))                 # an LCD steps from dot to dot
    o.append(circle(*P(66 + 4.5 * math.cos(th), 110 + 4.5 * math.sin(th)), 1.3 * SC + .2, L))
    m = 1 + round(3 * d)                                         # tone: a stair of harmonics
    for k in range(4):
        x = 77 + 3 * k if k < m else 300
        h = 9 - 2 * k
        o.append(path(pl([(x, 115), (x + 2, 115), (x + 2, 115 - h), (x, 115 - h)]), L))
    return o


def draw(a, b, c, d):
    o = []
    cx0, cy0, H = 142, 76, 7
    P = lambda x, y, z=H: iso(cx0, cy0, x, y, z)
    lw = 1.4                                                     # one weight for every line
    o += slab(cx0, cy0, 0, 0, 0, 104, 124, H, D2, 26, K)         # the deck: one rounded slab
    # freq: a mini disc or a full one, lying on the deck's top with a thin edge
    R = 30 if a < .5 else 44
    cx, cy = P(52, 56, H + 2)
    o += cylinder(cx, cy, R, 2, D6, side=D4)
    # scrub: the bow of light, two wedges in one outline, with a dim smear trailing each
    th = math.radians(lerp(-90, 270, c))
    ell = lambda t, r: (cx + r * math.cos(t) * 1.2247, cy + r * math.sin(t) * .7071)

    def fan(t0, t1, k):
        arc = lambda base: [ell(base + t0 + (t1 - t0) * i / k, R * .96) for i in range(k + 1)]
        return arc(th) + [ell(th + math.pi / 2, 3)] + arc(th + math.pi) + [ell(th - math.pi / 2, 3)]
    o.append(path(fan(math.radians(-55), math.radians(15), 10), D7))
    o.append(path(fan(math.radians(-15), math.radians(15), 6), L))
    o.append(ellipse(cx, cy, *iso_axes(12), D3, K, .5))          # the clear ring
    o.append(ellipse(cx, cy, *iso_axes(5.5), K))                 # the hole
    o += lcd(P, a, b, c, d)
    # the headphones, in the deck's projection: cups on the x axis, the band over them in the same plane
    hx0, hy0, zc = 352, 176, 40
    HQ = lambda x, y, z: iso(hx0, hy0, x, y, z)
    ring = lambda x, r: ring_x(hx0, hy0, x, r, 0, zc)

    def cup(sx):
        face, back, out = sx * 34, sx * 40, sx * 45
        housing = path(hull(ring(back, 11) + ring(out, 11)), D7, K, .5)
        pad = path(hull(ring(face, 16) + ring(back, 16)), D5, K, .5)
        if sx < 0:                                               # far cup: the pad's face turned toward us
            return [housing, pad, path(ring(face, 16), D6, K, .5)]
        return [pad, housing, path(ring(out, 11), L, K, .5)]     # near cup: the housing's end face in front
    o += cup(-1)

    # the wave, in the plane between the pads, from the middle of one pad's face to the other's
    def wv(u):
        ph = 2 * math.pi * (u * lerp(1.5, 3, a) + c * 2)
        v = math.sin(ph) + lerp(0, .6, d) * math.sin(3 * ph) / 3
        return v * min(1, max(0, (u - .05) / .12), max(0, (.95 - u) / .12))
    if b < .1:                                                   # crush at rest: the smooth wave
        uv = [(n / 20, wv(n / 20)) for n in range(21)]
    else:                                                        # held samples on coarse levels, fewer and coarser as it turns up
        S, q = round(lerp(15, 6, b)), round(lerp(6, 2, b))
        uv = []
        for k in range(S):
            v = round(wv((k + .5) / S) * q) / q
            uv += [(k / S, v), ((k + 1) / S, v)]
    pts = []
    for u, v in uv:
        p_ = HQ(lerp(-34, 34, u), 0, zc + v * 13)
        if not pts or abs(p_[0] - pts[-1][0]) > .05 or abs(p_[1] - pts[-1][1]) > .05:
            pts.append(p_)
    o.append(ribbon(pts, lw, L))
    near_cup = cup(1)
    band = outlined(ribbon([HQ(42 * math.cos(t), 0, zc + 12 + 34 * math.sin(t)) for t in [math.pi * k / 20 for k in range(21)]], 2.4, L))
    # the cord, from a plug on the deck's side, down off the screen and up into a plug under the near cup.
    # Both plugs are short cylinders in the scene's projection.
    pring = lambda x: ring_x(cx0, cy0, x, 2.8, 68, H / 2, 16)
    o.append(path(hull(pring(104) + pring(115)), D6, K, .5))
    o.append(path(pring(115), D5, K, .5))
    tx, ty = P(115, 68, H / 2)
    px, pb = HQ(39.5, 0, zc - 9)                                 # under the middle of the whole cup
    cup_plug = [cylinder_side(px, pb, *iso_axes(2.2), 13, D6, K, .5)]
    pb += 13
    sx_, sy_ = tx + .866 * 7, ty + .5 * 7                        # straight out along the plug's axis, then a smooth bend down
    lx = tx + 22
    mid, rad = (lx + px) / 2, (px - lx) / 2
    cord = [(tx, ty)] + bez((sx_, sy_), (sx_ + .866 * 9, sy_ + .5 * 9), (lx, sy_ + 6), (lx, sy_ + 20), 9) + [(lx, sy_ + 20), (lx, 226)]
    cord += [(mid - rad * math.cos(math.pi * k / 12), 226 + rad * math.sin(math.pi * k / 12)) for k in range(1, 12)]
    cord += [(px, 226), (px, pb - .5)]
    o.append(outlined(ribbon(cord, lw, D5)))
    o += near_cup[:1]                                            # the pad
    o += cup_plug                                                # the plug, over the pad and the cord's end
    o += near_cup[1:]                                            # the housing over the plug's top
    o.append(band)                                               # the band over everything
    return o
