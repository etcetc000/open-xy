# Display API

A synth draws its own page. Display code runs separately from audio. Each
frame it fills a bounded frame, which the SDK's browser and native renderers
apply to the machine's artwork, `panel.svg.fragment`. This is retained
drawing: the artwork's elements are drawn by the device, and the frame moves,
reshapes, recolours and relabels them. It is not executable SVG animation. The
same C++ computes the browser preview and the device frames; fonts, framing and
rasterization can differ.

There is one display contract. Characters, dials, keyboards, meters and other
patterns are the machine's own code filling nodes; see
[Macro Osc's segment characters](../machines/macroosc/segments.hpp).

## Declaration

Each synth declares its frame's budget in `resources.display` of
`definition.json` and names the same shape as its C++ frame type. The SDK sizes
the native renderer, the browser renderer and the artwork bindings from that
declaration; a mismatch fails to compile.

```json
"display": {"stateBytes": 60, "stateAlignment": 4,
            "paths": 2, "points": 64, "nodes": 28, "texts": 0, "palette": 16}
```

```cpp
using Frame = opxy::draw::Frame<2, 64, 28, 0, 16>;  // paths, points, nodes, texts, palette
void display_reset(DisplayState& state);
void display(DisplayState& state, const opxy::draw::Input& input, Frame& output);
```

| Field | Limits |
| --- | --- |
| `paths` | 0..4 mutable outlines; they are nodes `0..paths-1` |
| `points` | 2..64 points per outline; 0 when there are no paths |
| `nodes` | `paths`..256 transformable elements |
| `texts` | 0..4 text slots |
| `palette` | 1..16 paint entries |
| `stateBytes`, `stateAlignment` | `DisplayState` size (whole words) and alignment, checked against the compiled type |

[draw.hpp](../sdk/include/opxy/draw.hpp) is the format definition.
`DisplayState` is trivial, standard-layout data. Put it, with the frame type,
in `display.h`.

## Input

Input holds the Q31 value each of the four control positions shows, Q10 elapsed
seconds, track identity, a uint32 note-start serial, the reduced-motion
preference, `shown`, the declared parameter index each position shows (its own,
or an [alternate](api.md#alternate-controls)), and `slots`, the Q31 value of
every declared parameter by index (0 beyond the declared ones). A serial is a
snapshot, not a queue: several note starts between frames can coalesce.
Initializing or changing track must not fabricate a note.

## Frame

The frame persists between calls; the first call after reset must set every
element. Both renderers apply only what changed.

| Part | Contents |
| --- | --- |
| `paths[i]` | A nonzero geometry `key` and `points` points relative to the node's origin. Change the key whenever the points change; renderers cache the keys they applied. |
| `nodes[i]` | The element's whole transform, `x`, `y` (origin), `scale` (uniform) and `cos`, `sin` (a clockwise turn), plus `shade`, a palette entry, and `paint`: keep, stroke, or fill and stroke. |
| `texts[i]` | `x`, `y`, `scale` like a node's, without turning, and up to 23 characters. |
| `header[i]` | Text for control position `i` in place of the stock value, at its position and size: keep it to about five characters. An empty string keeps the stock value. |

| Field | Bounds |
| --- | --- |
| Path coordinates | ±`path_extent` (64) pixels, Q10 |
| Node and text x / y | `node_left`..`node_right` / `node_top`..`node_bottom` (-64..640 / -64..320) pixels, Q10 |
| Scale | Positive, at most 2, Q10 |
| Turn | A unit vector, Q10; `{unit, 0}` is upright |
| Palette selector | Below `palette` |
| Text | Terminated within its buffer |

An element authored in place, with no transform, is drawn where the artwork
puts it at `(0, 0)`, unit scale and no turn. `place` sets a node's transform
and paint upright, `turn` rotates it, `shade` repaints it without moving it,
and `hide` moves it to (`hidden_x`, `hidden_y`), outside the display, keeping a
positive unit scale. `rotation(tenths)` turns by tenths of a degree and
`sweep(value, half)` maps a control onto `half` tenths of a degree either side
of upright (at most 1800). `text` and `header` copy bounded strings. `valid`
checks the complete frame before painting; an invalid frame is not applied.

## Artwork

Elements bind by index and the machine's symbol prefix:

| Frame part | Artwork |
| --- | --- |
| Node `i` | `<prefix>_node_<i>`: a `path` or `circle`. Path nodes are a `path` of exactly `M`, `points - 1` `L` and `Z` with no fill. |
| Text slot `i` | `<prefix>_text_<i>`: a `text` with explicit font, positioned by its transform, with no `x` or `y` |
| Palette entry `i` | `<prefix>_palette_<i>`: a `path` or `circle` with explicit `#RRGGBB` fill and stroke and a stroke width; it lends them to the nodes that select it. Park entries off screen. |

Every frame sets a bound element's whole transform, so its authored transform
is `matrix(1 0 0 1 x y)` or none. Fill-and-stroke paint changes a node's fill
only when the artwork gives it one.

Use unique IDs, explicit paint on each leaf and the expected path topology.
Authored numbers allow at most four fractional digits; exponent notation and
unsupported elements/references are rejected. Do not rely on browser-only SVG
features or group style inheritance. The native packager combines the machine
fragment with the SDK-owned
[OP-XY screen template](../sdk/platform/opxy-1.2.0/screen.svg). It supplies a black
480 × 220 canvas and the stock synth header: a stepped grey bar 20 pixels tall
with four titles (10 px) above four values (20 px), light text on the dark half
and dark text on the light half, at the stock positions. Machine artwork starts
below y = 20. No stock artwork is copied. Machine graphics below that header
are controlled by `display.cpp`; the platform adapter updates parameter labels
and values.

The template's `titles` and `values` groups each contain four group children,
each with one explicitly styled text leaf. This is the stock four-knob helper's
binding contract. Keep it in the platform layer, shared by machines; a different
device can supply its own platform template and backend. The browser authenticates
the embedded template, while native packaging records it in source provenance.
Template validation checks structure, not actual device text rendering.

See [2op display.cpp](../machines/fm2op/display.cpp) (outlines and pulses) and
[Macro display.cpp](../machines/macroosc/display.cpp) (segment characters,
glow, dials and header text) for implementations.
How screens reach stock is described in the [firmware ABI](abi-1.2.0.md#screens).
