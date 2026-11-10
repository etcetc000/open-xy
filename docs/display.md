# Display API

A synth draws its own page, as from SDK 1.8 may a sample engine on the
sampler's page ([own screen](api.md#own-screen)). Display code runs separately from audio. Each
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

Each synth, or sample engine with its own screen, declares its frame's budget in `resources.display` of
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
| `nodes` | `paths`..512 transformable elements; more than 256 need `"sdk": "1.8"` |
| `texts` | 0..32 text slots; more than 4 need `"sdk": "1.3"`, more than 16 `"sdk": "1.7"` |
| `palette` | 1..16 paint entries |
| `stateBytes`, `stateAlignment` | `DisplayState` size (whole words) and alignment, checked against the compiled type |

[draw.hpp](../sdk/include/opxy/draw.hpp) is the format definition.
`DisplayState` is trivial, standard-layout data. Put it, with the frame type,
in `display.h`.

## Display activation

The native renderer calls `display_reset` when an owned synth page becomes
selected and when its selected track changes. The next display callback
starts with reset state; use this for an entrance or to establish fresh
note-activity baselines. Continuing to show the same page and track does not
reset it. Browser hosts call `PreviewDisplay.reset()` when reactivating a
preview. Audio state, note routing and the drawing input ABI are unchanged.

A display host that resets a parameter may notify the optional GUI-only callback
`display_parameter_reset(DisplayState&, u32 slot)` through
`draw::parameter_reset(state, slot)`. It can animate back to that parameter's
default without resetting the whole display. Omitting it is supported. This
lifecycle callback does not change `draw::Input` or its browser ABI.

## Input

Input holds the Q31 value each of the four control positions shows, Q10 elapsed
seconds, track identity, a uint32 note-start serial, the reduced-motion
preference, `shown`, the declared parameter index each position shows (its own,
or an [alternate](api.md#alternate-controls)), `slots`, the Q31 value of
every declared parameter by index (0 beyond the declared ones), note activity
and the track's amp envelope. A serial is a
snapshot, not a queue: several note starts between frames can coalesce.
Initializing or changing track must not fabricate a note. It counts per track,
not per key: where keys play different sounds, as a drum kit's do, an
animation started by the serial alone also plays for keys that weren't hit.

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
note activity does not promise that sound ends at a note-off (see the
[amp envelope](#amp-envelope-sdk-110)). Non-instrument tracks report zero.

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
Animate for as long as notes are held, then release: an animation of fixed
length after a note start stops while a long note still sounds.
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
Existing SDK 1.0 source remains supported. The browser host also accepts ABI 1
previews, which retain their 68-byte input and do not receive activity.
Rebuild old preview modules to use the new fields.

### Amp envelope (SDK 1.10)

`amp` holds the track's stock amplitude envelope, the one its envelope page
edits, on a synth's page or a sample engine's own screen: `attack`, `decay`, `sustain` and `release`, each a Q31 word from 0 to
`0x7fffffff` on the same scale as `slots`. Stock uses only each word's top
seven bits (`word >> 24`). A larger attack or decay is a longer stage, and a
larger sustain a higher level, as a linear gain. Release runs the other way: a
larger release is a shorter release. Stock reads it through decay's table,
mirrored, and its envelope page draws a larger release as a later start to a
shorter slope, so a preset's `release: 32767` is stock's shortest release.
These are control positions, not times, and stock maps them to times along its
own curves. The values are the ones stock's envelope page draws, read once per
frame on the GUI side: a snapshot for drawing, not a measurement of a voice's
envelope. A drum kit's voices play no amp envelope: stock's drum renderer
ignores these words, so a kit's voice plays until `finished`, a choke or a
steal, whatever its envelope page shows, and a kit's screen shouldn't follow
`amp`.

When the envelope cannot be read, every word is `envelope_unavailable` (-1);
`envelope_known(input.amp)` tests for it. Treat an unknown envelope as the
default the machine would otherwise show.

```cpp
// Slow a strike animation for slow attacks; short attacks keep their speed.
const opxy::i32 attack = opxy::draw::envelope_known(input.amp) ? input.amp.attack : 0;
```

Browser hosts pass `{attack, decay, sustain, release}` Q31 words as the
optional tenth argument to `PreviewDisplay.tick`, after the drum model, Shift
and its choices; omission reports the envelope as unknown, as the builder's
preview and the SDK workbench do.

Machines reading `amp` declare `"sdk": "1.10"`. New previews use drawing ABI 5,
a 104-byte input with `amp` after a drum kit's `model`; the browser host also
accepts the earlier ABIs, which keep their inputs and do not receive the
envelope.

### Effect pages (SDK 1.7)

An effect may draw its own page on the send tracks, as a synth draws its
engine page (see [the API](api.md#effect-pages)). Its input's `slots` hold all
eight of its controls, `track` is the send (14 or 15), and two fields follow
the note activity: `shift`, 1 while Shift is held on an effect with a
[shift row](api.md#effect-pages-and-shift-rows), and `choices`, each shift
control's choice (0 beyond the declared ones). A synth's page reads 0 in both.

### Shift and a drum kit's machine (SDK 1.8)

| Field | Meaning |
| --- | --- |
| `shift` | 1 while Shift is held on a page with Shift controls (a drum kit's), else 0. |
| `choices` | Each Shift control's choice, 0 beyond the declared ones. A drum kit with model groups gives its selected key's group, the model's place in the group and its choke group (0 none, 1..3); one without groups gives the model and the choke group. A [`padMix`](api.md#synthesis-drum-kits) kit gives the same, by whether it has groups, though its Shift row is the model, the choke group, pan and level: read `model`, `pan` and `level` for those. They hold whether or not Shift is held, so a screen can show them in its header either way. |
| `model` | A drum kit's selected key's model. |
| `pan`, `level` | A `padMix` kit's selected key's pan, -100..100 with 0 centred, and its level in whole dB as it plays, -30..0 (SDK 1.10; 0 dB on a key the kit has not set up); while a step is held, as that step plays them with its locks; 0 for other machines. |
| `line` | The row the encoders edit, 0 the first and 1 the second; on a drum kit, also the Shift row (SDK 1.10). |
| `pad` | A `padMix` kit's selected key's [pad fields](api.md#synthesis-drum-kits): its two pad controls, a signed one negative below 0, then FX I and FX II (0..7, 0 following the track's send); while a step is held, as that step plays them with its locks (its sends' overrides included); 0 when neutral, on a key the kit has not set up and for other machines (SDK 1.10). |

A screen that draws its own header (the two-page header) shows the Shift
controls in its edited row while `shift` is 1, as the stock header does. The
previews use drawing ABI 6, a 112-byte input: ABI 5 (104 bytes) ends after the
amp envelope, ABI 4 (88 bytes) after `model`, ABI 3 (84 bytes) after `choices`.

## Frame

The frame persists between calls; the first call after reset must set every
element. Both renderers apply only what changed.

| Part | Contents |
| --- | --- |
| `paths[i]` | A nonzero geometry `key` and `points` points relative to the node's origin. Change the key whenever the points change; renderers cache the keys they applied. |
| `nodes[i]` | The element's whole transform, `x`, `y` (origin), `scale` (uniform) and `cos`, `sin` (a clockwise turn), plus `shade`, a palette entry, and `paint`: keep, stroke, or fill and stroke. |
| `texts[i]` | `x`, `y`, `scale` like a node's, without turning, and up to 23 characters. |
| `header[i]` | Text for control position `i` in place of the stock value, at its position and size: keep it to about five characters. An empty string keeps the stock value. |

Standalone native hosts without the synth template can opt into the same header
contract by authoring text leaves named `<prefix>_header_value_0` through
`<prefix>_header_value_3`, and optional labels `<prefix>_header_title_0` through
`<prefix>_header_title_3`. These leaves keep their authored position, font and
paint; they do not consume or alias animated text slots. The shared platform
header adapter binds them and applies `Frame.header`. A standalone host restores
the shown parameter's declared label and normalized 0..100 value each frame;
nonempty `Frame.header[i]` overrides that value. Supply mapped units and enum
captions in the display callback. Missing leaves are supported and add no visual
header. Synths retain their stock template and stock mapped-value formatting.

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
consecutive bar nodes and text slots 0..15 (a machine on `"sdk": "1.7"`
may declare up to 32 and use 16 onward for its own); keep the authored header
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

### Sample overview

From SDK 1.8, a sample engine's [own screen](api.md#own-screen) can draw its sample.
[sample_overview.hpp](../sdk/include/opxy/sample_overview.hpp) gives display
code `opxy_sample_overview()`, never null: the selected track's sample as the
engine plays it.

| Field | Contract |
| --- | --- |
| `generation` | Changes whenever a different overview is published, including when the same file is replaced or reloaded; never 0 once loaded. Redraw a waveform when it moves |
| `assigned` | 0 while the track holds no sample: say how to load one |
| `loaded` | 0 until the overview is ready, while the sample loads and the file worker reads it. The fields below it are then 0, never the last sample's |
| `frames`, `rate`, `channels` | The whole sample's length, rate and channels |
| `begin`, `end`, `reverse` | The outer markers, in frames, as the engine plays them, and 1 if it plays backwards. These follow edits in any frame |
| `low`, `high` | 96 bins of equal length across the whole sample: the smallest and largest 16-bit value of any channel in each |
| `gain` | SDK 1.10: the sample's gain, the one Shift + encoder 4 edits on the stock sampler's page (or a [`stockShift`](api.md#own-screen) `gain` encoder), in stock's whole steps: -30 at the bottom of its travel, +20 at the top, 0 as a sample is assigned. It follows edits in any frame. The level the engine receives (`SampleBlock.region.level`) follows it along stock's own curve. `gain_unavailable` while the track holds no sample |

The file worker reads the sample in the background; the screen only copies the
result, so a long sample never stalls a frame, and the overview follows the sample
through replacement, eviction and reload. Audio callbacks never call it.
In the browser preview it stays empty unless the host writes one through the
`opxy_display_sample` export.

## Artwork

Elements bind by index and the machine's symbol prefix:

| Frame part | Artwork |
| --- | --- |
| Node `i` | `<prefix>_node_<i>`: a `path` or `circle`. Path nodes are a `path` of exactly `M`, `points - 1` `L` and `Z`, every point distinct: the OP-XY's SVG parser merges repeated points, leaving fewer than the frame binds, and the device then no longer reshapes the outline. |
| Text slot `i` | `<prefix>_text_<i>`: a `text` with explicit font, positioned by its transform, with no `x` or `y` |
| Palette entry `i` | `<prefix>_palette_<i>`: a `path` or `circle` with explicit `#RRGGBB` fill and stroke and a stroke width; it lends them to the nodes that select it. Park entries off screen. |

Mutable paths may have an explicit `#RRGGBB` fill when their authored geometry encloses area.
Use a nonzero initial fill for shapes that must stay opaque: both renderers
preserve `fill="none"` as disabled when applying palette changes.

Every frame sets a bound element's whole transform, so its authored transform
is `matrix(1 0 0 1 x y)` or none: the pose before the first frame, which the
frame then replaces.

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

### Drawing on the device

The OP-XY draws some artwork differently from the browser preview. Each of
these was found on hardware:

- **Outlines enclose area.** A line traced out and back over itself, or an
  outline that turns straight back at a point, has none there, and the OP-XY
  draws streaks from its ends. Draw a line as a thin closed ribbon: one edge
  out, the other back a fraction of a pixel away, every point distinct.
  `sdk.py test` warns about outlines that break this in its preview frames.
- **Avoid turns close to straight back.** A nearly reversing turn can draw too: on
  the device, one thin tooth sent a long line up the screen. We treat under about
  2.5° from straight back as a fault, an inferred limit: 2.57° has drawn fine. Thin,
  tall shapes are the usual cause, so give a sharp tip a small flat end.
- **Points stay 1/8 px apart.** Neighbouring points closer than 1/8 px merge,
  as repeated points do, and the outline then stops reshaping. This holds for
  a ribbon's two edges too.
- **Start an outline where both edges are long.** A closing edge, from the
  last point back to the first, shorter than 1/4 px draws as a long sliver
  along the next edge.
- **Fills come from the artwork.** Only an element authored with a fill is
  ever filled. Fill-and-stroke paint recolours that fill but never adds or
  removes one, so author solid parts with a fill. A mutable outline authored
  `fill="none"` always draws hollow; one authored with a fill stays filled
  (see above).
- **One subpath, at most 64 points.** Keep every authored `path`, bound or
  not, to one subpath of at most 64 points: artwork with dozens of subpaths
  and hundreds of points in a path stopped the device at launch. Split a large
  part into several elements.
- **Parts that meet share an outline or a pivot.** Separately transformed
  parts that touch in the preview can land apart on the device. Draw them as
  one outline, or as nodes with the same origin and transform.

## Grayscale artwork

A synth targeting SDK 1.5 can offer its own grayscale design:

```json
"sdk": "1.5",
"graphics": {"grayscale": "panel.grayscale.svg.fragment"}
```

This is an optional machine.json entry. The path names a separate SVG fragment
inside the machine, at most 64 KiB; redirects and paths outside the machine are
rejected. Start with a copy of `panel.svg.fragment`, then choose grayscale fills,
strokes, text colors and off-screen palette donors. Retain every bound node,
text and palette ID and its element type. Retain bound origins, ancestor
transforms, fill versus none, and mutable-outline topology and distinct points.
Static geometry, stroke widths, labels and brightness can be designed for
grayscale. Both versions use the same display.cpp, resource budget and sound.
For palette choices and visual review, consider the optional
[color design guidance](ux.md#color-design-guidance): prioritize meaningful
hue-based distinctions over matching source brightness, with consistent roles
across static artwork and animation palettes.

`sdk.py test --machine <folder>` and preview builds validate both SVGs. All paint
in the alternate, including decorative elements and animation palettes, must be
grayscale. Invalid variants fail with an error; they are never silently converted.
The workbench's Graphics selector previews both versions using the current
controls and animation state. Check notes, releases, control extremes, page
changes and reduced motion in both modes. Review meaningful hue-based
distinctions, the closest gray tones and their backgrounds across the full
animation palette. Validation alone cannot judge clarity.

On the installer's **Extensions** page, **Grayscale machine graphics** keeps
an already-grayscale original byte for byte; otherwise it uses the validated
developer variant unchanged, or automatic conversion when no variant is declared.
The checkbox reflects whether any selected custom screen is configured for
grayscale. **Configure** can set individual screens to grayscale or original;
checking or unchecking the option applies that choice to all selected custom screens.
The automatic fallback builds one palette from every fill and stroke, including
off-screen animation donors. It groups strongly chromatic colors into hue
families and assigns separate gray ranges to those families, prioritizing hue
distinctions over matching original brightness. Brightness remains a secondary
cue within each range. Allocation adapts to the number of unique chromatic
colors, hue families and shades: smaller palettes distinguish closer hues;
single-color families consume no shade band, and multi-shade families share
at most a quarter of the usable range. The rest separates hue families across
gray levels 96–248, retaining visibility on black backgrounds. Repeated colors
and aliases do not consume extra tones. Exact neutral grays remain unchanged;
weak tints and palettes with only one hue family retain luminance-based
conversion. This keeps subdued backgrounds and grids from becoming bright
accents. The mapping is
independent of SVG order and repeated paint usage, and stays fixed throughout
animation. It is a palette heuristic: many hues, opacity and overlapping shapes
can still limit visible separation. Supply a deliberate grayscale variant when
automatic conversion does not preserve the intended distinctions.
The selection happens while packaging, without a firmware drawing ABI change or
a new saved sound parameter. Preview artifacts authenticate the variant's hash
and source alongside the original; rebuilding is required after editing either.

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
