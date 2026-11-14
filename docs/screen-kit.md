# Screen kit

The screen kit is a set of Python parts for sketching a synth screen that already follows
the [synth screens](synth-screens.md) guide: the stock greys, isometric shading and outlines,
and the display API's limits. You sketch the screen with it and check its cost first, then
write `panel.svg.fragment` and `display.cpp` once the sketch looks right. It doesn't make
firmware or machine files.

## Start from an example

Each of the guide's examples has a source file in `tools/screen_kit/examples/`, such as
[`blinds.py`](../tools/screen_kit/examples/blinds.py). Start from the two or three
closest to your idea, borrowing parts from each, in a screen file in your machine's
repository. List them with:

```sh
python ../open-xy/tools/screen_kit/preview.py --list
```

A screen file sets `LABELS`, the four control names, and `draw(a, b, c, d)`, which takes
each control from 0 to 1 and returns the drawing. `HERO` (a chosen setting), `NAME`,
`SDK` (`"1.8"` for more than 256 nodes) and `LIKE` (the guide examples to compare with) are
optional.

```python
from tools.screen_kit.kit import *

LABELS = ['low', 'mid', 'high', 'air']
LIKE = ['sliding-slabs', 'cube-ring']

def draw(a, b, c, d):
    o = []
    for i, v in enumerate((a, b, c, d)):   # four cubes in their encoder greys, each lifted by its control
        o += box(150 + 60 * i, 120, 0, 0, lerp(0, 50, v), 30, 30, 30, CODE3[i])
    return o
```

## Preview it

```sh
python ../open-xy/tools/screen_kit/preview.py screen.py --out local/screen-001
```

This makes one contact sheet (`sheet.svg`, and `sheet.png` on macOS). It shows the screen
with every control at 0 and at 100, at `HERO` (or every control at 50), and with each
control at 100 while the others stay at 50. It also shows the `LIKE` examples from the
guide, at 0 and at 100. Every frame, 50 included, is saved in `frames/`.
`report.txt` and the top of the sheet list:

- the nodes, and how many outlines change shape: the display API reshapes only four, and
  every other node can only move, turn, scale or change grey;
- the most nodes that change in one 5% turn of each control (the guide asks for about 20
  a frame);
- any control whose change is easy to miss: a part under about 30 px, or parts that move
  less than about 8 px without changing grey;
- where the drawing departs from what the stock screens measure: a black outline on a
  solid part that isn't 0.5 px, a part closer than about 18 px under the header (a part
  hanging from the header is fine), or a drawing under 140 px across (stock subjects
  span 170 to 230 px), or a part running off the screen's edge. These are worth a look,
  not errors;
- an outline that changes shape and spans more than 256 px: the display API writes its
  points within 64 px of its node and scales a node at most 2 (a fixed outline may be any
  size);
- anything the display API would refuse: more than 64 points in an outline, a repeated
  point, a grey that isn't in the guide, or too many nodes. The command then exits with 1.

To keep to four reshaping outlines, give a part that changes size fixed copies and show
one while the others are parked (Barcode's thin and wide bars, Split's tiles), join parts
that change together into one outline (Blinds' slat pairs), or cut one long bending line
into at most four pieces (Rotary phone's cord).

## Beside the stock screens

Put the stock screens you studied on the same sheet as your draft:

```sh
python ../open-xy/tools/screen_kit/preview.py screen.py --out ~/opxy-stock/sheet-001 --stock ~/opxy-stock/hardsync.png ~/opxy-stock/epiano.png ~/opxy-stock/simple.png ~/opxy-stock/prism.png
```

`--stock` takes PNG and JPEG screenshots as well as SVG files, so the official guide's
pictures go on the sheet as saved, as do any files from the person's own OP-XY
([study the stock screens](synth-screens.md#study-the-stock-screens)). Each is scaled to
the 480 × 220 screen. Make this sheet for every draft, with the four stock screens
closest to your idea, and judge the draft on it: size, weight, how much is lit. The stock
screens take the place of the `LIKE` examples, keeping the sheet short enough to stay sharp;
`--like` adds examples back. The stock screens are for looking only. They and `--out`
must sit outside every checkout, so the sheet that shows them never lands in a
repository; the command refuses otherwise.

## Compare versions

When a spot needs work, make two or three small versions of the screen file and let the
person pick one from a single sheet:

```sh
python ../open-xy/tools/screen_kit/preview.py screen_a.py screen_b.py screen_c.py --out local/pick-001 --control tone
```

The sheet letters them A, B and C. With `--control`, each shows that control at 0 and at
100 with the others at the chosen setting; without it, each shows its chosen setting and
every control at 100. Change one spot per sheet, so the person compares only that, and
keep the version they pick.

## The parts

All in [`tools/screen_kit/kit.py`](../tools/screen_kit/kit.py):

| Part | What it draws |
| --- | --- |
| `K`, `D1`..`D7`, `L`, `RAMP`, `CODE`, `CODE3` | The guide's greys, and the four encoder greys for flat and 3D parts |
| `path`, `circle`, `rect`, `rrect`, `ellipse`, `round_poly` | Flat shapes: sharp `rect` by default, `rrect` and `round_poly` for round objects and silhouettes |
| `line`, `ribbon`, `quarter_arc`, `hatch` | Lines drawn as thin outlines with no repeated points; `hatch` shades a see-through area |
| `iso`, `box`, `slab`, `plate`, `cylinder`, `ring_x`, `hull` | Isometric parts: side faces one grey darker and 0.5 px black outlines |
| `turned`, `outlined`, `PARKED` | Turn a part, outline a flat mark, and park a hidden part off screen |
| `cell_random`, `BAYER`, `euclid_hits` | Rules for fields: the same random value for a cell every time, ordered dither, even rhythms |
| `render`, `lint`, `measure`, `report` | The whole screen with the SDK's header, and the checks above |

The kit sketches the screen; the machine draws it. Each part in the sketch becomes a node
in `panel.svg.fragment`, and `display.cpp` moves, turns, scales, repaints or parks it as
the sketch does ([display API](display.md)).
