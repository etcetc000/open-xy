# Display API

A synth, or a sample engine declaring a display, draws its own page. Display code runs separately from audio. Each
frame it fills a bounded frame, which the SDK's browser and native renderers
apply to the machine's artwork, `panel.svg.fragment`. This is retained
drawing: the artwork's elements are drawn by the device, and the frame moves,
reshapes, recolours and relabels them. It is not executable SVG animation. The
same C++ computes the browser preview and the device frames; fonts, framing and
rasterization can differ.

The [UX conventions](ux.md) say how machine screens look and move.

There is one display contract. Characters, dials, keyboards, meters and other
patterns are the machine's own code filling nodes; see
[Macro Osc's segment characters](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/segments.hpp).

## Declaration

Each drawing machine declares its frame's budget in `resources.display` of
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
| `texts` | 0..16 text slots; more than 4 need `"sdk": "1.3"` |
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

Sample displays can read a copied waveform overview, trim and voice activity
through [`opxy::sample_display`](api.md#sample-engines). This supplies data for
ordinary paths and nodes in the same frame; it adds no renderer or display
class. Browser display previews return an unavailable sample snapshot.

### Note activity (SDK 1.1)

| Field | Meaning |
| --- | --- |
| `note_on_serial` | Alias of `note_serial`, the existing note-start counter. |
| `held_notes` | Number of distinct MIDI pitches held on this track, 0..128. Repeated starts of the same pitch count once until released. |
| `note_off_serial` | Counter of held pitches released. Duplicate releases do not increment it. Like note starts, it wraps modulo 2^32. |

This is note gate activity, independent of oscillator voices, voice stealing,
and amplitude release tails. On the device it follows stock's per-track
note-on/off map, which stock also uses to illuminate its keyboard; it is not a
physical-key sensor or a measurement of audible sound. Sequenced and live notes
follow the same stock note routing. Sustain/latch behavior follows that routing;
this API does not read the amplitude envelope or promise that sound ends at a
note-off. Non-instrument tracks report zero.

Store a zero-initialized `opxy::draw::NoteTracker` in `DisplayState` and call
`update(input)` once per frame. Call the tracker's `reset()` in
`display_reset`, and include its 16 bytes in the declared `stateBytes`.
It returns `NoteEvents`:

| Event | Meaning |
| --- | --- |
| `hold_started` | The previous frame held no pitches and this frame holds at least one. |
| `hold_ended` | The previous frame held pitches and this frame holds none. |
| `note_released` | `note_off_serial` changed, including when other pitches remain held. |

```cpp
const auto events = state.notes.update(input);  // notes is a NoteTracker member
if (events.hold_started) { /* begin the held animation */ }
if (events.hold_ended) { /* begin its release */ }
if (events.note_released) { /* react to one or more releases */ }
const bool held = input.held_notes <= 128 && input.held_notes != 0;
```

The first update, an update after reset, and a track change establish a
baseline and emit no events, even if notes are already held. While `held` is
true, accumulate `elapsed` in the machine's state to measure animation time.
These are frame snapshots: a complete press/release between frames can leave
the held count unchanged, while both serials advance. Such a release still
sets `note_released`; hold edges only describe the observed frames.

Browser hosts pass `{held_notes, note_off_serial}` as the optional sixth
argument to `PreviewDisplay.tick`; the third argument remains the note-start
serial. `NoteActivity` from `sdk/browser/display.mjs` maintains these counters
through `noteOn(note)`, `noteOff(note)` and `panic()`, independently of audio.
One instance describes one track. Omission reports `notes_unavailable`
(`0xffffffff`) in `held_notes`, which clears the tracker's baseline. The SDK
workbench and builder both supply activity, including releases with reduced
motion enabled.

Machines using these additions declare `"sdk": "1.1"` in `machine.json`.
Existing SDK 1.0 source remains supported. New previews use drawing ABI 2 with
a 76-byte input; the browser host also accepts existing ABI 1 previews, which
retain their 68-byte input and do not receive activity. Rebuild old preview
modules to use the new fields.

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
and `hide` moves it to (`hidden_x`, `hidden_y`), the bottom-right corner
(640, 320), keeping a positive unit scale. That is 160 pixels right of and 100
below the 480 × 220 screen, so a hidden element stays out of view unless it
reaches more than 160 pixels left and more than 100 pixels up from its origin.
`rotation(tenths)` turns by tenths of a degree and
`sweep(value, half)` maps a control onto `half` tenths of a degree either side
of upright (at most 1800). `text` and `header` copy bounded strings. `valid`
checks the complete frame before painting; an invalid frame is not applied.

## Two-page header

SDK 1.4 supplies `<opxy/page_header.hpp>` for synths using `"press": "page"`.
`sdk.py new --controls 8` generates it with eight controls; `--controls 5`,
`6` or `7` shares unchanged controls between the rows. The default four-control
starter keeps the stock header. This helper uses the existing drawing ABI and
the [UX layout](ux.md#more-than-four-controls).

Store `opxy::draw::PageHeader` in `DisplayState` and call its `reset()` from
`display_reset`. It occupies 20 bytes with four-byte alignment. Reserve eight
consecutive bar nodes and all sixteen text slots; keep the authored header
block from the generated `panel.svg.fragment`. That block includes the stock
header cover, stepped bar and bright/grey text at their resting sizes. If
moving its node bindings, update both the artwork IDs and `first_node` below.
Keep other artwork below y=38 and do not cover the header later in the SVG.

```cpp
const char* const titles[8] = {"wave", "shape", "level", "tone",
                               "drift", "noise", "", ""};
// Six controls: columns 2 and 3 repeat their original controls on page 2.
const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 2, 3}};
char values[8][opxy::draw::header_capacity] = {};
for (opxy::u32 i = 0; i < 6; ++i)
    opxy::draw::header_amount(values[i], sizeof(values[i]), input.slots[i]);
opxy::draw::page_header(state.header, input, frame, titles, values, first_node, pages);
```

Titles and captions are indexed by saved parameter slot. Supply lowercase
real-unit captions for enums, frequencies, ratios and other mapped controls;
`header_amount` rounds Q31 amounts to 0..100 without a percent sign. Titles
are lowercased by the helper. The page map must match each alternate's
`control` assignment; omit it only for slots 4..7 in column order. Shared
columns repeat the original slot index, so labels, values and edits agree.

Rows keep their order while the active row grows and brightens. Transitions
settle within 0.15 seconds and reverse continuously on a second press.
Initialization, track changes and reduced motion snap to the selected page.
The helper reads every `shown` position, including layouts whose first
column is shared. It does not touch the machine's audio or parameter state.

## Artwork

Elements bind by index and the machine's symbol prefix:

| Frame part | Artwork |
| --- | --- |
| Node `i` | `<prefix>_node_<i>`: a `path` or `circle`. Path nodes are a `path` of exactly `M`, `points - 1` `L` and `Z` with no fill, every point distinct: the OP-XY's SVG parser merges repeated points, leaving fewer than the frame binds, and the device then no longer reshapes the outline. |
| Text slot `i` | `<prefix>_text_<i>`: a `text` with explicit font, positioned by its transform, with no `x` or `y` |
| Palette entry `i` | `<prefix>_palette_<i>`: a `path` or `circle` with explicit `#RRGGBB` fill and stroke and a stroke width; it lends them to the nodes that select it. Park entries off screen. |

Every outline a frame draws must enclose area. A line traced out and back over
itself, or an outline that turns straight back at a point, has none there, and the
OP-XY draws streaks from its ends. Draw a line as a thin closed ribbon: one edge
out, the other back a fraction of a pixel away, every point distinct. `sdk.py test`
warns about outlines that break this in its preview frames.

Every frame sets a bound element's whole transform, so its authored transform
is `matrix(1 0 0 1 x y)` or none: the pose before the first frame, which the
frame then replaces. Fill-and-stroke paint changes a node's fill
only when the artwork gives it one.

Use unique IDs, explicit paint on each leaf and the expected path topology.
Authored numbers allow at most four fractional digits; exponent notation and
unsupported elements/references are rejected. Do not rely on browser-only SVG
features or group style inheritance. The native packager combines the machine
fragment with the SDK-owned
[OP-XY screen template](../sdk/platform/opxy-1.2.1/screen.svg). It supplies a black
480 × 220 canvas and the stock synth header: a stepped grey bar 20 pixels tall
with four titles (10 px) above four values (20 px), light text on the dark half
and dark text on the light half, at the stock positions. Machine artwork usually
starts below y = 20. No stock artwork is copied. Machine graphics below that header
are controlled by `display.cpp`; the platform adapter updates parameter labels
and values. The machine's artwork is drawn after the template, so artwork that
covers the top 20 pixels hides the stock header: a machine can draw its own
header there with nodes and text slots. The browser previews draw the stock
header under the artwork in the same way.

The template's `titles` and `values` groups each contain four group children,
each with one explicitly styled text leaf. This is the stock four-knob helper's
binding contract. Keep it in the platform layer, shared by machines; a different
device can supply its own platform template and backend. The browser authenticates
the embedded template, while native packaging records it in source provenance.
Template validation checks structure, not actual device text rendering.

See [2op display.cpp](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/fm2op/display.cpp) (outlines and pulses) and
[Macro display.cpp](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/display.cpp) (segment characters,
glow, dials and header text) for implementations.
How screens reach stock is described in the [firmware ABI](abi-1.2.1.md#screens).

## Migrating from before SDK 1.0

Machines written for the earlier beta use two display contracts that SDK 1.0
replaced with this one. To bring one up to date:

- `machine.json` declares `"sdk": "1.0"`.
- A `"kind": "drawing"` display drops its `kind` and declares `"texts": 0`.
- `Frame<paths, points, nodes, palette>` becomes
  `Frame<paths, points, nodes, 0, palette>`.
- Artwork IDs carry the machine's ID, not `local`: `<id>_panel`,
  `<id>_node_<i>`, `<id>_palette_<i>`.
- A mutable outline's points are all distinct in the artwork: the device
  merges repeated points.
- A `"kind": "segments"` display, or code that includes `opxy/segments.hpp`,
  draws with this contract instead. Macro Osc keeps its own
  [segments.hpp](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/segments.hpp) and
  [display.cpp](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/display.cpp): copy the parts you need into
  your machine and declare nodes for them.
- Code that fills a `Node` directly also sets its rotation (`cos`, `sin`);
  `place`, `shade` and `turn` do this for you.

Then run `python sdk.py test --machine <folder>`.
