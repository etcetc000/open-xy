"""Parts for drawing an OP-XY synth screen the way the stock screens are drawn.

A screen is a function of the four controls, each 0..1, that returns a list of SVG elements
(strings): `draw(a, b, c, d) -> [element, ...]`. Every element is a path of one subpath with
at most 64 distinct points, a circle or text, as the display API can bind them, so the kit
can count nodes and measure how much a control redraws before any display code is written.
The look these parts carry is described in docs/synth-screens.md; this file only makes it
easy to keep. Coordinates are screen pixels, 480 x 220, with the header across y 0..20.
"""
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from tooling.formats import require

# The greys a drawing may use (docs/synth-screens.md, "The grey code").
K, D1, D2, D3, D4, D5, D6, D7, L = ('#000000', '#191919', '#323232', '#4B4B4B', '#646464',
                                    '#7D7D7D', '#969696', '#AFAFAF', '#CDCDCD')
RAMP = [K, D1, D2, D3, D4, D5, D6, D7, L]
GREYS = frozenset(RAMP)
CODE = (D1, D3, D6, L)    # encoder 1..4 on flat parts
CODE3 = (D2, D3, D6, L)   # encoder 1..4 on 3D parts: encoder 1 in 323232, so its black outline reads
OUTLINE = .5              # every face of a 3D part: a 0.5 px black outline

W, H, HEADER = 480, 220, 20


def lerp(a, b, t):
    return a + (b - a) * t


def darker(grey, steps=1):
    """The grey `steps` ramp steps darker (never below black): the shading of a side face."""
    return RAMP[max(RAMP.index(grey) - steps, 0)]


def num(v):
    s = f'{v:.2f}'.rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def _paint(fill, stroke=None, sw=None):
    s = f' fill="{fill}"'
    if stroke:
        s += f' stroke="{stroke}" stroke-width="{sw}"'
    return s


# --- flat elements -------------------------------------------------------------------------

def path(pts, fill=K, stroke=None, sw=None):
    """A closed outline through `pts`. Repeated neighbouring points are dropped, as the SDK needs."""
    pts = [p for i, p in enumerate(pts)
           if i == 0 or (num(p[0]), num(p[1])) != (num(pts[i - 1][0]), num(pts[i - 1][1]))]
    require(len(pts) <= 64, f'an outline has {len(pts)} points; the display API allows 64')
    d = 'M' + ' L'.join(f'{num(x)} {num(y)}' for x, y in pts) + ' Z'
    return f'<path d="{d}"{_paint(fill, stroke, sw)}/>'


def circle(x, y, r, fill, stroke=None, sw=None):
    return f'<circle cx="{num(x)}" cy="{num(y)}" r="{num(r)}"{_paint(fill, stroke, sw)}/>'


def text(x, y, t, fill=K, size=10):
    return f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" font-size="{size}" fill="{fill}">{t}</text>'


def outlined(element, sw=OUTLINE):
    """Give an already built path or circle a black outline."""
    return element.replace('/>', f' stroke="{K}" stroke-width="{sw}"/>', 1)


def rect(x, y, w, h, fill, stroke=None, sw=None):
    """Sharp corners: flat shapes keep them unless the object is round."""
    return path([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], fill, stroke, sw)


def rrect_pts(x, y, w, h, r, steps=3):
    out = []
    for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            out.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return out


def rrect(x, y, w, h, r, fill, stroke=None, sw=None):
    """Rounded corners: round objects, symbol tiles and silhouettes."""
    return path(rrect_pts(x, y, w, h, r), fill, stroke, sw)


def ellipse(cx, cy, rx, ry, fill, stroke=None, sw=None, k=40):
    return path([(cx + rx * math.cos(2 * math.pi * i / k), cy + ry * math.sin(2 * math.pi * i / k)) for i in range(k)],
                fill, stroke, sw)


def round_poly(pts, r, k=3):
    """A polygon with every corner rounded by radius r (at most 45% of the shorter edge): a silhouette."""
    out = []
    n = len(pts)
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        a = (p0[0] - p1[0], p0[1] - p1[1])
        b = (p2[0] - p1[0], p2[1] - p1[1])
        la, lb = math.hypot(*a), math.hypot(*b)
        rr = min(r, la * .45, lb * .45)
        s_ = (p1[0] + a[0] / la * rr, p1[1] + a[1] / la * rr)
        e_ = (p1[0] + b[0] / lb * rr, p1[1] + b[1] / lb * rr)
        for j in range(k + 1):
            t = j / k
            u = 1 - t
            out.append((u * u * s_[0] + 2 * u * t * p1[0] + t * t * e_[0], u * u * s_[1] + 2 * u * t * p1[1] + t * t * e_[1]))
    return out


def line(x1, y1, x2, y2, w, fill):
    """A straight line of width w, as a thin four-point outline (a hairline at w = 1)."""
    a = math.atan2(y2 - y1, x2 - x1)
    ox, oy = -math.sin(a) * w / 2, math.cos(a) * w / 2
    return path([(x1 + ox, y1 + oy), (x2 + ox, y2 + oy), (x2 - ox, y2 - oy), (x1 - ox, y1 - oy)], fill)


def ribbon(pts, w, fill):
    """A line through `pts`, as one thin closed outline: out along one edge, back along the other.
    Up to 32 points; split a longer line into several ribbons that share their end points."""
    up, down = [], []
    for i, (x, y) in enumerate(pts):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        t = math.atan2(b[1] - a[1], b[0] - a[0])
        ox, oy = -math.sin(t) * w / 2, math.cos(t) * w / 2
        up.append((x + ox, y + oy))
        down.append((x - ox, y - oy))
    return path(up + down[::-1], fill)


def quarter_arc(cx, cy, r, a0, w, fill, k=6):
    """A quarter circle band of width w about (cx, cy), starting at angle a0 (radians)."""
    outer = [(cx + (r + w / 2) * math.cos(a0 + math.pi / 2 * i / k), cy + (r + w / 2) * math.sin(a0 + math.pi / 2 * i / k)) for i in range(k + 1)]
    inner = [(cx + (r - w / 2) * math.cos(a0 + math.pi / 2 * i / k), cy + (r - w / 2) * math.sin(a0 + math.pi / 2 * i / k)) for i in range(k + 1)]
    return path(outer + inner[::-1], fill)


def hatch(poly, angle, gap, w, fill):
    """Parallel lines across a convex polygon, every `gap` px at `angle` degrees: how stock shows
    a see-through area. Each line is its own node, so keep hatching to small areas."""
    t = math.radians(angle)
    ux, uy = math.cos(t), math.sin(t)     # along the lines
    nx, ny = -uy, ux                      # across them
    across = [x * nx + y * ny for x, y in poly]
    out = []
    s = math.ceil(min(across) / gap) * gap
    while s <= max(across):
        hits = []
        for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]):
            c0, c1 = x0 * nx + y0 * ny - s, x1 * nx + y1 * ny - s
            if (c0 < 0) != (c1 < 0):
                f = c0 / (c0 - c1)
                hits.append((x0 + (x1 - x0) * f, y0 + (y1 - y0) * f))
        if len(hits) >= 2:
            hits.sort(key=lambda p: p[0] * ux + p[1] * uy)
            (x0, y0), (x1, y1) = hits[0], hits[-1]
            if math.hypot(x1 - x0, y1 - y0) > w:
                out.append(line(x0, y0, x1, y1, w, fill))
        s += gap
    return out


def bez(p0, p1, p2, p3, k=8):
    """k points along a cubic curve, without its end point (so curves chain)."""
    out = []
    for i in range(k):
        t = i / k
        u = 1 - t
        out.append((u**3*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t**3*p3[0], u**3*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t**3*p3[1]))
    return out


def turned(pts, cx, cy, deg, dx=0, dy=0):
    """`pts` turned by deg about (cx, cy), then moved by (dx, dy): how a part moves without reshaping."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(cx + dx + (x - cx) * c - (y - cy) * s, cy + dy + (x - cx) * s + (y - cy) * c) for x, y in pts]


def hull(pts):
    """The convex outline around `pts`: the silhouette of a round part seen from an angle."""
    pts = sorted(set((round(p[0], 2), round(p[1], 2)) for p in pts))
    lo, hi = [], []
    for p in pts:
        while len(lo) >= 2 and (lo[-1][0] - lo[-2][0]) * (p[1] - lo[-2][1]) - (lo[-1][1] - lo[-2][1]) * (p[0] - lo[-2][0]) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(hi) >= 2 and (hi[-1][0] - hi[-2][0]) * (p[1] - hi[-2][1]) - (hi[-1][1] - hi[-2][1]) * (p[0] - hi[-2][0]) <= 0:
            hi.pop()
        hi.append(p)
    return lo[:-1] + hi[:-1]


# A part not showing waits off screen, at the display API's hidden corner, so the frame keeps
# the same nodes whatever the controls do.
PARKED = circle(640, 320, 1, K)


# --- isometric -----------------------------------------------------------------------------
# Stock's 3D parts sit in one isometric projection: x runs down-right, y down-left, z up.

def iso(cx, cy, x, y, z=0):
    return (cx + (x - y) * .866, cy + (x + y) * .5 - z)


def iso_axes(r):
    """The screen radii (rx, ry) of a circle of radius r lying flat in the projection."""
    return (r * 1.2247, r * .7071)


def box(cx, cy, x, y, z, w, d, h, T, left=None, right=None, top=True):
    """A box: front-left and top faces in T, front-right face one ramp step darker, each face
    outlined in 0.5 px black. With top=False and its own side greys it is a plate under a
    drawn top (a board's squares)."""
    R = right or darker(T)
    P = lambda a, b, c: iso(cx, cy, a, b, c)
    o = [path([P(x, y + d, z + h), P(x + w, y + d, z + h), P(x + w, y + d, z), P(x, y + d, z)], left or T, K, OUTLINE),
         path([P(x + w, y, z + h), P(x + w, y + d, z + h), P(x + w, y + d, z), P(x + w, y, z)], R, K, OUTLINE)]
    if top:
        o.append(path([P(x, y, z + h), P(x + w, y, z + h), P(x + w, y + d, z + h), P(x, y + d, z + h)], T, K, OUTLINE))
    return o


def plate(cx, cy, w, d, th, left, right):
    """The two visible sides of a thin plate whose top (drawn separately) lies at z = 0."""
    return box(cx, cy, 0, 0, -th, w, d, th, left, left, right, top=False)


def rounded_plan(x, y, w, d, r, k=6):
    """A rounded rectangle in plan, as (point, outward normal angle) pairs."""
    out = []
    for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + d - r, 0), (x + r, y + d - r, 90), (x + r, y + r, 180)):
        for i in range(k + 1):
            a = a0 + 90 * i / k
            t = math.radians(a)
            out.append(((cx + r * math.cos(t), cy + r * math.sin(t)), a % 360))
    return out


def _facing(plan, lo, hi):
    return [p for p, a in plan if lo <= (a + 180) % 360 - 180 <= hi]


def slab(cx, cy, x, y, z, w, d, h, T, r=7, outline=K):
    """A slab with rounded corners in plan: front-left side in T, front-right side one step
    darker, top in T. Three outlines, each with a 0.5 px outline in `outline`."""
    P = lambda a, b, c: iso(cx, cy, a, b, c)
    plan = rounded_plan(x, y, w, d, r)
    seam = (outline, OUTLINE) if outline else ()
    o = []
    for lo, hi, c in ((45, 135, T), (-45, 45, darker(T))):
        f = _facing(plan, lo, hi)
        f = sorted(f, key=lambda q: -q[0]) if lo == 45 else sorted(f, key=lambda q: q[1])
        o.append(path([P(a, b, z + h) for a, b in f] + [P(a, b, z) for a, b in f[::-1]], c, *seam))
    o.append(path([P(a, b, z + h) for a, b in [q for q, _ in plan]], T, *seam))
    return o


def cylinder_side(cx, cy, rx, ry, h, fill, stroke=None, sw=None, k=24):
    """The side band of an upright cylinder whose top ellipse is centred on (cx, cy)."""
    lo = [(cx + rx * math.cos(math.pi * i / k), cy + ry * math.sin(math.pi * i / k)) for i in range(k + 1)]
    return path(lo + [(x, y + h) for x, y in lo[::-1]], fill, stroke, sw)


def cylinder(cx, cy, r, h, T, side=None):
    """An upright cylinder (a disc, a puck, a post) with its top centred on (cx, cy): side band
    one step darker than its top unless `side` is given, both outlined."""
    rx, ry = iso_axes(r)
    return [cylinder_side(cx, cy, rx, ry, h, side or darker(T), K, OUTLINE), ellipse(cx, cy, rx, ry, T, K, OUTLINE)]


def ring_x(cx, cy, x, r, y=0, z=0, k=24):
    """A circle of radius r standing across the x axis at x: an end of a cylinder lying along x."""
    return [iso(cx, cy, x, y + r * math.cos(2 * math.pi * i / k), z + r * math.sin(2 * math.pi * i / k)) for i in range(k)]


# --- fields --------------------------------------------------------------------------------

def cell_random(*key):
    """A fixed pseudo-random number in 0..1 for a cell, so the same cell always decides the
    same way: a control then only says how much, and the field never flickers."""
    x = 0
    for v in key:
        x = (x * 1103515245 + int(v) * 2654435761 + 12345) & 0xFFFFFFFF
    x ^= x >> 13
    x = (x * 0x5bd1e995) & 0xFFFFFFFF
    x ^= x >> 15
    return (x & 0xFFFF) / 65536


BAYER = [[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38],
         [60, 28, 52, 20, 62, 30, 54, 22], [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
         [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]]


def euclid_hits(k, steps=16):
    """The steps of k hits spread as evenly as possible over `steps` (a Euclidean rhythm)."""
    return [i for i in range(steps) if k and ((i * k) // steps != ((i - 1) * k) // steps if i else True)]


# --- the screen ----------------------------------------------------------------------------

TEMPLATE = Path(__file__).resolve().parents[2] / 'sdk/platform/opxy-1.2.1/screen.svg'
SVG = 'http://www.w3.org/2000/svg'


def header(labels, values):
    """The stock four-value header, from the SDK's own screen template, with these titles
    and values (0..100) in place of its placeholders."""
    ET.register_namespace('', SVG)
    root = ET.fromstring(TEMPLATE.read_bytes())
    groups = {g.get('id'): g for g in root}
    for name, words in (('titles', labels), ('values', [str(round(v * 100)) for v in values])):
        for leaf, word in zip(groups[name], words):
            leaf[0].text = word
    return root


def render(draw, values, labels):
    """The whole 480 x 220 screen at these control values, as an SVG document."""
    root = header(labels, values)
    elements = draw(*values)
    root.append(ET.fromstring(f'<g xmlns="{SVG}" id="drawing">{"".join(elements)}</g>'))
    return ET.tostring(root, encoding='unicode')


# --- checks --------------------------------------------------------------------------------

POINT = re.compile(r'[ML](-?[\d.]+) (-?[\d.]+)')
PAINT = re.compile(r'(?:fill|stroke)="([^"]+)"')
SETTINGS = ((0, 0, 0, 0), (.5, .5, .5, .5), (1, 1, 1, 1))


def lint(draw, sdk='1.0'):
    """Problems the display API would refuse, as a list of sentences (empty when it fits)."""
    problems = []
    limit = 512 if tuple(map(int, sdk.split('.'))) >= (1, 8) else 256
    for v in SETTINGS + tuple(tuple(1 if i == c else .5 for i in range(4)) for c in range(4)):
        elements = draw(*v)
        nodes = sum(e.startswith(('<path', '<circle')) for e in elements)
        if nodes > limit:
            problems.append(f'{nodes} nodes; sdk {sdk} allows {limit}' + ('' if limit == 512 else ' (512 with "sdk": "1.8")'))
        for e in elements:
            if not re.match(r'<(path|circle|text) ', e):
                problems.append(f'only paths, circles and text can be bound: {e[:40]}')
                continue
            for grey in PAINT.findall(e):
                if grey != 'none' and grey.upper() not in GREYS:
                    problems.append(f'{grey} is not one of the guide\'s greys')
            if e.startswith('<path'):
                pts = POINT.findall(e)
                if len(pts) > 64 or e.count('M') != 1:
                    problems.append(f'an outline needs one subpath of at most 64 points: {e[:60]}')
                if len(set(pts)) != len(pts):
                    problems.append(f'an outline repeats a point: {e[:60]}')
    return list(dict.fromkeys(problems))


def _points(e):
    """An element's points, on screen or off: a path's points, or a circle's bounding corners."""
    if e.startswith('<circle'):
        x, y, r = (float(re.search(f' {k}="([-\\d.]+)"', e).group(1)) for k in ('cx', 'cy', 'r'))
        return [(x - r, y - r), (x + r, y + r)]
    return [(float(x), float(y)) for x, y in POINT.findall(e)]


def _span(e):
    pts = _points(e)
    if not pts:
        return 0
    return max(max(x for x, _ in pts) - min(x for x, _ in pts), max(y for _, y in pts) - min(y for _, y in pts))


def _shape(e):
    """A path's points, or a circle as its centre and radius."""
    if e.startswith('<circle'):
        return [complex(float(re.search(f' {k}="([-\\d.]+)"', e).group(1)), 0) for k in ('cx', 'cy', 'r')]
    return [complex(float(x), float(y)) for x, y in POINT.findall(e)]


def _similar(a, b, tolerance=.05):
    """Whether outline b is outline a moved, turned or scaled: what a node transform can do."""
    if len(a) != len(b):
        return False
    if len(a) < 3:
        return True
    ca, cb = sum(a) / len(a), sum(b) / len(b)
    a = [p - ca for p in a]
    b = [p - cb for p in b]
    norm = sum(abs(p) ** 2 for p in a)
    if norm == 0:
        return True
    z = sum(q * p.conjugate() for p, q in zip(a, b)) / norm
    return max(abs(q - z * p) for p, q in zip(a, b)) <= tolerance


def measure(draw):
    """(nodes, texts, outlines that change shape, most nodes changed per 5% step of each control).

    The display API can move, turn and scale a node and repaint it every frame for free; only
    its four `paths` can change shape. Stock aims for about 20 changed nodes a frame."""
    nodes, texts, reshaping, peak = _sweep(draw)
    return nodes, texts, len(reshaping), peak


def _sweep(draw):
    base = draw(.5, .5, .5, .5)
    nodes = sum(e.startswith(('<path', '<circle')) for e in base)
    texts = sum(e.startswith('<text') for e in base)
    peak, reshaping = [], {}
    for c in range(4):
        worst = 0
        frames = []
        for s in range(21):
            p = [.5] * 4
            p[c] = s / 20
            frames.append(draw(*p))
        for A, B in zip(frames, frames[1:]):
            require(len(A) == len(B), 'a drawing must keep the same number of elements at every setting; park unused parts off screen')
            worst = max(worst, sum(x != y for x, y in zip(A, B)))
        peak.append(worst)
        for i, e in enumerate(frames[10]):
            if e.startswith('<path') and any(not _similar(_shape(e), _shape(f[i])) for f in frames):
                reshaping[i] = max([reshaping.get(i, 0)] + [_span(f[i]) for f in frames])
    return nodes, texts, reshaping, peak


def _box(e):
    """The on-screen points of an element."""
    return [(x, y) for x, y in _points(e) if 0 <= x <= W and 0 <= y <= H]


def _diff(A, B):
    """The size (w, h) of the area that differs between two drawings, and the most any point
    moves (None where a part changes grey or shape)."""
    pts, travel = [], 0
    for x, y in zip(A, B):
        if x == y:
            continue
        pts += _box(x) + _box(y)
        p, q = _box(x), _box(y)
        if travel is None or PAINT.findall(x) != PAINT.findall(y) or len(p) != len(q) or not p:
            travel = None
        else:
            travel = max(travel, max(math.dist(u, v) for u, v in zip(p, q)))
    if not pts:
        return (0, 0), 0
    xs, ys = [x for x, _ in pts], [y for _, y in pts]
    return (max(xs) - min(xs), max(ys) - min(ys)), travel


def change(draw):
    """For each control, sweeping it from 0 to 100 with the others at 50, the largest change from
    its setting at 0: the size (w, h) of the area that changes and the most any point moves, or
    None where a part changes grey or shape. A change you have to hunt for doesn't count, so
    report() flags small ones."""
    out = []
    for c in range(4):
        frames = []
        for k in range(11):
            v = [.5] * 4
            v[c] = k / 10
            frames.append(draw(*v))
        w = h = travel = 0
        for f in frames[1:]:                                     # a control may come back round to its start
            (fw, fh), t = _diff(frames[0], f)
            w, h = max(w, fw), max(h, fh)
            travel = None if t is None or travel is None else max(t, travel)
        out.append(((w, h), travel))
    return out


SMALL_AREA, SMALL_TRAVEL = 30, 8
SMALL_SUBJECT = 140   # stock subjects span 170 to 230 px; under this the drawing looks unfinished
PATH_BOX = 256      # a changing outline's points stay within 64 px of its node, scaled at most 2
CLEAR = 38          # stock leaves about 30 px under the header; a part may touch the header on purpose


def stock_notes(draw):
    """Where the drawing departs from what the stock screens measure (docs/synth-screens.md),
    as sentences: worth a look, not errors."""
    notes, highest, overrun = [], None, {}
    for values in SETTINGS:
        for e in draw(*values):
            stroke = re.search(r'stroke="([^"]+)"', e)
            width = re.search(r'stroke-width="([^"]+)"', e)
            fill = re.search(r'fill="([^"]+)"', e)
            if stroke and stroke.group(1).upper() == K and fill and fill.group(1) != 'none' \
                    and width and abs(float(width.group(1)) - OUTLINE) > .01:
                notes.append(f'a black outline is {float(width.group(1)):g} px; stock outlines solid parts at {OUTLINE:g} px')
            tops = [y for _, y in _box(e)]
            if tops and HEADER < min(tops) < CLEAR:
                highest = min(tops) if highest is None else min(highest, min(tops))
            pts = _points(e)
            if any(0 <= x <= W and HEADER < y <= H for x, y in pts):          # on screen, not parked
                for x, y in pts:
                    for edge, past in (('left', -x), ('right', x - W), ('bottom', y - H)):
                        if past > 1:
                            overrun[edge] = max(overrun.get(edge, 0), past)
    for edge, past in overrun.items():
        notes.append(f'a part runs {past:.0f} px past the {edge} edge of the screen; keep the subject on '
                     'screen unless it runs off on purpose')
    if highest is not None:
        notes.append(f'a part reaches y {highest:.0f}, {highest - HEADER:.0f} px under the header; stock leaves about 30 px clear unless a part hangs from the header or runs off the edge on purpose')
    pts = [p for e in draw(*SETTINGS[1]) for p in _box(e) if p[1] > HEADER]
    if pts:
        w = max(x for x, _ in pts) - min(x for x, _ in pts)
        h = max(y for _, y in pts) - min(y for _, y in pts)
        if max(w, h) < SMALL_SUBJECT:
            notes.append(f'the drawing spans only {w:.0f} x {h:.0f} px; stock subjects span 170 to 230 px across, so draw it bigger')
    return list(dict.fromkeys(notes))


def report(draw, labels, sdk='1.0'):
    """measure() and lint() as lines a person can read."""
    nodes, texts, changing, peak = _sweep(draw)
    reshaping = len(changing)
    lines = [f'{nodes} nodes, {texts} texts, {reshaping} outlines change shape; most nodes changed per 5% step: '
             + ', '.join(f'{name} {p}' for name, p in zip(labels, peak))]
    if reshaping > 4:
        lines.append(f'{reshaping} outlines change shape; the display API reshapes 4 (`paths`), so move, turn, '
                     'scale or swap parked copies of the rest')
    span = max(changing.values(), default=0)
    if span > PATH_BOX:
        lines.append(f'an outline that changes shape spans {span:.0f} px; the display API writes its points within '
                     f'64 px of its node and scales a node at most 2, {PATH_BOX} px in all, so cut it into pieces')
    if max(peak) > 20:
        lines.append(f'up to {max(peak)} nodes change in one step; aim for about 20 a frame while notes sound')
    for name, ((w, h), travel) in zip(labels, change(draw)):
        if max(w, h) < SMALL_AREA:
            lines.append(f'{name} changes only a {w:.0f} x {h:.0f} px area from 0 to 100: easy to miss; give it a bigger part')
        elif travel is not None and travel < SMALL_TRAVEL:
            lines.append(f'{name} moves its parts at most {travel:.0f} px from 0 to 100: easy to miss; move them further or change their grey')
    return lines + stock_notes(draw) + lint(draw, sdk)
