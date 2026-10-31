# Engine API

A machine is a folder under `machines/` named after the machine. Every kind has
`machine.json` and `definition.json`; the C++ it supplies depends on its kind:

| Kind | C++ the machine supplies | Example |
| --- | --- | --- |
| `synth` | `engine.cpp` with `Voice`/`Shared` state, `display.cpp` and `panel.svg.fragment`; `control.cpp` with [track control](#track-control) | [2op FM](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/fm2op/engine.cpp), [Macro Osc](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/engine.cpp) |
| `sample` | `sample.cpp` with `Voice` state; the stock sampler it forks provides its screen | [Timestretch](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/timestretch/sample.cpp) |
| `effect` | `fx.cpp` with `State`; the stock FX page is its screen, or its own (SDK 1.7) | [Ring Mod](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/ringmod/fx.cpp) |

The [first-machine walkthrough](first-machine.md) creates a standalone synth
and previews it without native integration. SDK adapters own allocation,
C linkage and stock registration.

## Platform rules

These come from the stock firmware, not from SDK convention. Breaking one
fails the build or, for the artwork rule, halts the device's SVG parser.

| Area | Rule |
| --- | --- |
| Machine folder | C++ source, definition JSON, `machine.json` and artwork only. No build scripts, generators or tests |
| C++ | Freestanding C++11: no exceptions, RTTI, static constructors, allocation, OS headers, `#if`, inline assembly or platform macros |
| Callbacks | Bounded work per 64-sample block; no blocking, logging, file access or graphics from audio callbacks |
| Synth controls | Four control positions, plus up to four alternates; values arrive as Q31 and the stock menu stores Q15 defaults |
| Audio | 64 frames at 44.1 kHz, Q31 output peaking at most at half scale; mono by default, optional stereo for sample engines |
| Identity | The name follows the [identity rules](#identity) and never changes once published; a firmware's [layout](#identity) gives the numbers |
| Sample engines | The sample is read-only and borrowed for the voice's lifetime |
| Effects | State fits in 476 bytes and holds no resources (no destructor ever runs) |
| Artwork | At most four decimal places and no exponent notation in any number; explicit paint on every leaf; no `style`, events or external references |

The [firmware contract](abi-1.2.1.md) explains where each rule comes from.

## Memory and limits

Every selected machine and the SDK share **one 2032 KiB module region**: code,
constant data and zero-initialized state together. It is the 2 MiB the
firmware reserves from its heap, minus 16 KiB of relocated stock tables. There
is no per-machine allowance: a build that does not fit names each machine's
share, and `sdk.py compile` and the builder's memory meter report the same
figures. State usually dominates, since a synth's voice pool is
`voiceBytes × (min(voiceCapacity, 27) + 1)`: stock never sounds more than 27
notes, and one spare voice serves a note that ended as another starts. The SDK
adds 12 bytes per stock voice index (3 KiB) and 16 per voice.

The limit applies to what someone installs, not to the whole catalog. When a
collection doesn't fit at once, `sdk.py compile` links Open-XY's own machines
and requires each other machine to fit beside them, as the builder offers it.

| Limit | Value | Why |
| --- | --- | --- |
| Module memory | 2032 KiB for the selected machines | Firmware heap reservation minus the relocated tables |
| Voices | 1..256 per machine | Stock voice indices |
| Effect state | 476 bytes | The 480-byte stock FX slot minus its object header |
| Synth controls | 4 positions, at most 8 parameters | The stock four-knob page and eight-word parameter vector |
| Sample and effect controls | 1..8 | The stock eight-word parameter vector |
| Control labels | 8 characters | The stock parameter page |
| Display shape | ≤ 4 paths of ≤ 64 points, ≤ 256 nodes, ≤ 16 text slots (more than 4 need `"sdk": "1.3"`), ≤ 16 palette entries | Fixed-size renderer object and frame |

`voiceBytes`, `sharedBytes` and display state have no limit of their own
beyond whole words and module memory. Browser previews run 8 voices in
2 MiB of fixed WebAssembly memory, with growth disabled. This browser allowance
does not change the native collection's module limit or measure its footprint.

## CPU

The OP-XY renders every voice and effect in 64-sample blocks, 1.45 ms each, on
one Blackfin core that also runs its own engines, sequencer and effects. A
machine's cost adds up per sounding voice and per track that uses it. The SDK
sets no hard limit and does not yet measure device cycles.

`python sdk.py test` prints an estimate for synths and effects in **Blackfin
instructions per 64-sample block**, at the machine's heaviest control setting.
It times the machine's render (or `process`) on your computer against a
reference loop in the same run, a table oscillator through a one-pole filter
of known Blackfin instruction count (about 590 per block), so the figure is
about the same on any computer:

```text
CPU estimate (host-timed; not measured on the device): Karplus about 7,700 Blackfin instructions per voice block at its heaviest setting. Aim for at most 15,000; for scale, 2op FM about 14,000, Macro Osc (Braids) about 9,000
```

| Per 64-sample block | Aim for at most | Likely to drop audio above | Core machines |
| --- | --- | --- | --- |
| A synth voice | 15,000 | 24,000 | 2op FM about 14,000, Macro Osc (Braids) about 9,000 |
| An effect, once per track | 30,000 | 48,000 | Ring Mod about 10,500 |

Above the aim, make the machine lighter where you can; above the second limit,
measure it on a device with many notes held across tracks. Instructions are
not cycles: pipeline stalls add some. And the estimate understates what the
host does fast and the Blackfin does not:

- **Division** has no hardware instruction and costs tens of cycles on the
  device. Divide once per block, or use a table or a reciprocal.
- **Floating point** has no hardware either. Keep audio in integers;
  [`q31.hpp`](../sdk/include/opxy/q31.hpp) has the fixed-point helpers: a
  saturating multiply, and (SDK 1.7) a saturating add, one instruction each on the device.
- **64-bit arithmetic** has no runtime helpers in the module: keep products
  within 32 bits or split them, as `q31.hpp` does.
- **Per-sample work that changes only per block**, such as coefficients and
  increments, belongs outside the sample loop.

Measure and publish the required [hardware CPU readout](profiling.md) before
submitting a catalog entry. Host estimates do not satisfy that requirement.

## Definition

Every machine kind keeps its controls in `definition.json` (for example
[2op FM's](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/fm2op/definition.json)); the
[validator](../tooling/definitions.py) rejects missing or unknown fields.

| Field | Contract |
| --- | --- |
| `schema` | 1 |
| `id` | The machine's name; also its C++ namespace |
| `name` | Display name |
| `parameters` | Ordered controls, each with `id`, `label`, `default`, `mapping`, `display`: four to eight for a synth (see [alternate controls](#alternate-controls)), one to eight for a sample engine or effect; an effect's after the fourth are alternates on its send page. Labels are at most eight letters, digits or spaces |
| `presets` | Named maps containing exactly the declared controls; may be empty |
| `resources` | What the kind declares about its storage (below) |

Controls are finite numbers in [0, 1]. Mappings support `linear`
(`minimum + (maximum - minimum) * value`), three or more increasing `anchors`
interpolated evenly, `power` (`maximum * value^exponent`, exponent other than 1),
`exponential` (`minimum * (maximum/minimum)^value`) and `enum`. Display formats
are `ratio`, `percent` (the normalized value scaled to 0.0–100.0, without a suffix), `bipolar` (a -1..1 value as a signed
percentage), `duration`, `semitones`, `enum` and `division` (SDK 1.2: one of
stock delay's eight note lengths, below). Mappings describe
the control for previews and the builder; the C++ applies its own mapping. Enum quantization matches the native Q31-to-Q15 path.
On the device, a synth's or send effect's enum of up to 255 choices moves one
choice per encoder detent (two when turned fast), in plain and locked edits
alike, and stores the middle of the choice's range; a larger enum keeps stock's
0..99 stepping. Its value text on the engine or send page is the selected
choice's label.
Parameter order and mapping changes affect saved sounds; the
[UX conventions](ux.md) cover direction, defaults and adding parameters. The stock parameter
page shows labels in lower case.

### Alternate controls

A synth's first four parameters belong to the four encoder positions of its
page. Up to four more are alternates: each also names its position with
`"control": 0..3`. Pressing that encoder cycles the position through its own
parameter, then its alternates in parameter order; turning it edits the one
shown, whose title and stock percentage the page shows. Every parameter keeps
its own slot in the eight-word vector, so `render` receives all of them and
presets, projects and the catalog's defaults store each one. The selection is
per track, starts on the position's own parameter and is not saved. The track
menu's scramble randomizes alternates along with the first four; an empty
placeholder among the alternates keeps its value. Display
inputs carry each position's shown value, its parameter index (`shown`) and
every parameter's value (`slots`), so a [header value](display.md#frame)
can format whichever is shown while dials follow their own parameters.
Browser previews show the parameter last edited at each position.

A send effect's parameters after the first four are alternates on its send
page in the same way. Each may name its position with `"control": 0..3`;
without one, parameter `i` alternates on position `i - 4`. A press cycles the
position, its turns edit the parameter shown, and the page shows that
parameter's title and value. When an effect is put on a send, the SDK stores
its fifth to eighth defaults as stock stores its first four, so `process`
receives them before they are first edited. `"press": "page"` is for synths
only. A send effect's LFOs keep stock's four destinations, its first four
parameters.

A synth with five to eight parameters may declare `"press": "page"` at the
top of `definition.json`: pressing any encoder switches the whole page. Each
position may have at most one alternate. Positions without an alternate show
and edit their original parameter on both pages, with one saved value, not a
duplicate parameter. Eight-parameter pages require SDK 1.3; partial second
pages require SDK 1.4. The four `shown` entries identify the selection: look
for any alternate, since the first column may be shared.

Browser previews select a page when editing one of its distinct parameters;
editing a shared control keeps the current page. Use the SDK's
[two-page header](display.md#two-page-header) for fixed rows, active-row
emphasis and motion. `sdk.py new` supplies it by default. Macro Osc is the
[worked reference](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/definition.json).

A synth may leave a slot empty yet keep later parameters at their indices
(projects save by index): declare it
`{"id": ..., "label": "", "default": 0, "mapping": {"kind": "linear", "minimum": 0, "maximum": 1}, "display": "percent", "absent": true}`,
with no preset entry. It belongs to no position, so no press selects it; an
empty own slot still starts its position's cycle, so the press reaches the
position's alternates. Its title is empty and a turn on it changes nothing; a
machine's display can blank its value with header text `" "`. A paged synth
may give an absent slot after the first four a `control`: that position's
second page is then the empty slot, blank and inert, rather than its
first-page parameter again.

On instrument tracks, the Value, Random and Element LFOs can target every
declared parameter of a custom synth or sample engine, including all eight
when present. Destination names follow parameter order regardless of which
alternate the engine page shows. Synth destination icons follow each
parameter's assigned encoder, including alternates that share an encoder.
Sample-engine icons repeat for the second row of four controls. Turning
stops at the first and last declared destination; pressing cycles through
the declared destinations and wraps to the first. Extended machines use
eight selector positions, bounded to their declared count. A retained
destination beyond the machine's count resolves to its last parameter in
audio, labels and icons. Stock engines and
machines with at most four parameters retain stock selection. When opening
projects saved with older firmware, reselect LFO engine destinations on
machines with more than four parameters: their saved selector values now
address eight slots instead of four.

On custom synth engine pages, Shift + turn edits the same visible parameter
at one tenth of the normal rate. Enum choices keep their one-choice stepping.
Synth amounts and bounded positive anchor-ratio captions show one decimal
place; amounts omit the percent suffix. A machine's display can replace them with
its own [header text](display.md#frame). Custom send effects also
use the slower Shift rate for continuous controls, with stock FX captions
except for `enum` and `division` controls.

A sample engine declares `voiceCapacity`, which must be 256 (stock creates a
voice for each of its 256 voice indices and binds each to its sample), and the
optional [sample services](#sample-engines) it uses. An effect declares no resources (`{}`).
A synth declares:

| Field | Required value or meaning |
| --- | --- |
| `voiceBytes`, `voiceAlignment` | `Voice` size (whole words) and alignment, checked against the compiled type |
| `voiceCapacity` | Notes that sound at once, 1..256. Stock plays at most 8 notes per track and fades a note it takes, so 8 keeps a machine on one track within its own voices; allow 8 for each track it plays on at once. Values above 27, stock's voice slots, keep 27 voices plus the spare (28). Stock's 256 voice indices share these voices and one spare ([synth voices](abi-1.2.1.md#synth-voices)) |
| `sharedBytes` | `Shared` size, or 0 when `Shared` is empty |
| `outputPeakQ31` | Positive declared oscillator peak; the preview uses it for audition gain |
| `display` | The screen's frame budget; see the [display API](display.md) |
| `trackControl` | Optional: the [track control](#track-control) state's `stateBytes` and `stateAlignment`, and optionally `unique`, `savedBytes` and `marks` |

The SDK never runs constructors or destructors for `Voice`, `Shared` or
`DisplayState`: their storage starts zeroed and the callbacks set it up.
Exceptions and allocation are not supported.

## Audio buffers and ownership

[opxy/audio.hpp](../sdk/include/opxy/audio.hpp) defines the callback types.

| Argument | Contract |
| --- | --- |
| Voice state | SDK-owned, correctly aligned storage; the engine initializes all sound fields |
| `Output` | Writable 64-word mono signed Q1.31 buffer; write every sample |
| `Parameters` | Read-only normalized Q31 words, eight; the declared controls in order, then zero |
| `Pitch` | Read-only 64-word unsigned phase-increment buffer, modulo 2^32 cycles |

Pointers are non-null, aligned and non-overlapping. Do not retain borrowed
buffer pointers beyond the call. `round(normalized * 2147483647)` produces a
control word. Physical units are the engine's mapping, not the buffer format.
Use [Q31 helpers](../sdk/include/opxy/q31.hpp) for defined multiply/rounding;
phase arithmetic wraps instead of saturating like audio.

Callbacks perform bounded work without allocation, blocking, file access,
logging or graphics. Device amplitude envelopes, filters and mixing follow the
oscillator callback; the browser supplies a simpler audition gate and mixer.

## Lifecycle

Every synth implements four callbacks in its declared namespace:

```cpp
struct Voice { ... };   // one voice's state
struct Shared { ... };  // state shared by every voice; empty when nothing is shared

void initialize(Voice& voice, Shared& shared, opxy::u32 owner);
void start(Voice& voice, Shared& shared, opxy::u32 owner);
void destroy(Voice& voice, Shared& shared, opxy::u32 owner);
void render(Voice& voice, Shared& shared, opxy::u32 owner,
            opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch);
```

Storage begins zeroed; `owner` is a stable nonzero voice ID, the stock voice
index plus one. Each of the machine's `voiceCapacity` voices serves one owner at
a time: `initialize` runs when a voice goes to an owner and establishes all
sound state; `start` runs on each note; `destroy` releases anything the voice
holds in `Shared`, once stock has stopped rendering its note (it ended, or
stock took it and restarted the index) or another note has taken the voice.
The adapter notices an ended note within about three blocks while the machine
renders other notes; after the machine's last note it notices at the next note
start. A taken note renders one last block fading to silence, then renders
silence; no voice index ever fails, whatever the capacity.
Callbacks run serially per voice. The browser preview's voice stealing and
panic use the same callbacks, but the preview always keeps 8 voices and takes
the oldest at once: it does not model `voiceCapacity`, stock's renders, the
spare or the fade. The SDK adapter owns the firmware object header,
method table and voice pool; none of that belongs in `Voice`.

[LeasePool](../sdk/include/opxy/lease_pool.hpp) provides fixed typed resources.
`acquire` reuses an owned slot or clears the free/oldest slot; `get` verifies
ownership; `release` cannot free a stolen lease. The engine decides how a
voice behaves after losing a resource. This pool has no locks and requires
serialized access.

## Track control

A synth can steer other tracks (SDK 1.7). A build without such a machine
applies none of track control's firmware edits and links none of its code. Such a synth runs on an
[aux track](#aux-tracks), never in an instrument track's place. It declares
`"trackControl": {"stateBytes": n, "stateAlignment": a, "unique": true, "aux": [12]}`
in `resources`, with `"sdk": "1.7"` in `machine.json`, and supplies
`control.cpp` with:

```cpp
struct ControlState { ... };  // trivial, standard-layout; starts zeroed
void control_reset(ControlState& state);
void control(ControlState& state, const opxy::control::Input& input, opxy::control::Overlay& overlay);
```

[track_control.hpp](../sdk/include/opxy/track_control.hpp) defines the types.
`control` runs on the GUI side once per stock GUI tick while its machine holds
an aux track ([aux tracks](#aux-tracks)). Currently only track 12 is supported,
so one controller runs in each pattern. `unique: true` is required: it has one
`ControlState`. `opxy::control::copies(track)` reports the number of tracks
holding that machine (browser previews answer 1). `control_reset` runs before
its first call, and again only after a project is loaded or created, just
before `control_load` (below). The machine keeps its state while patterns that
hold its role (CV) play, and when it comes back.

| `Input` field | Contract |
| --- | --- |
| `track` | The aux track the machine holds, zero-based |
| `capturing` | 1 while this copy's capture is in effect (below) |
| `values[track][index]` | Every track's 58 live parameter words (`all_tracks`, 16; aux tracks are 8..15), as stock sets them (edits, parameter locks, sequencing), without any machine's overlay. Indices 0..7 are the engine's controls; the rest are stock's own track parameters, in stock's units. The machine's own engine controls are the values its page shows, including its LFO |
| `edit_count`, `edits` | While capturing: up to 32 encoder edits of other instrument tracks' parameters since the last tick, oldest first, as track, index and edited value |
| `elapsed_us` | Microseconds since the previous tick, from the core clock: 0 on the first tick after a reset, at most one second |
| `transport` | The project's tempo in tenths of BPM (0 when unknown), the song position in 96ths of a quarter note, which advances only while playing, and whether the sequencer plays |
| `page_key` | Last directly handled page key since the last tick: 1..4 for M1..M4, or 0. M1 returns to the machine page; M2/M3 leave the current page alone. With `sharedLfo: true`, M4 belongs to the shared LFO and is not delivered |
| `muted` | 1 while the machine's track is muted in the current scene (aux + track key, or the mixer). As with stock's brain, a muted machine has no effect: none of its overlays, capture or marks apply. It still runs, so its page can say so |

`overlay` arrives empty; `opxy::control::add` appends up to 64 entries, each a
track, parameter index and value for this tick. Entries for the machine's own
track or outside the table are ignored, the last entry for a parameter wins and
negative values count as 0. When two machines overlay one parameter, the one on
the later track wins. A parameter no machine overlays any more returns to
stock's value. Overlays are live values only: saved projects keep stock's
values, and an edit or parameter lock under an overlay reaches `values` on the
next tick. Pages show an overlaid parameter's overlay, whether or not the
sequencer plays; for a moment after the parameter's encoder is turned, they
show the track's own value, which the turn changes.

Setting `overlay.capture` to 1 asks to capture encoder edits for the next tick:
they still play and show as usual, and arrive in `edits`. One copy captures at a
time: the one already capturing while it asks, otherwise the first track
asking; `input.capturing` tells a copy its capture is in effect. When the
capturing copy returns 0, or leaves its track, every parameter edited during the capture
returns to its value from before it, so a machine can record edits without
keeping them. Restoration is tied to the edited pattern, including when another
pattern is now playing; it does not replace a subsequent external base write.
Loading or creating a project discards outstanding edits and overlays without
restoring them into the replacement project. Edits made while a step is held (parameter locks) and changes
from MIDI or presets are not captured. Like the display, `control` is bounded
and allocation-free; it is not an audio callback. Browser previews do not run
it.

An overlay lasts one tick: a machine adds every entry it still wants each tick,
and a parameter it stops adding gets its underlying value back at the end of
that tick. The underlying value is stock's: what `values` reports, including
edits, parameter locks and sequencing under the overlay. Between machines, the
later track wins a parameter, and the earlier machine's overlay comes back when
the later one gives it up; with track 12 the only aux track for now, one machine
runs at a time, so no two overlay together. All machines together overlay at most 128 different
parameters in a tick; entries beyond that are ignored. A capture restores at
most 128 different parameters: further edits during it play and stay as edited,
and do not arrive in `edits`.

A unique machine can keep data with each project, such as Morphose's states.
It adds `"savedBytes": n` (1..8192) to `trackControl` and supplies:

```cpp
u32 control_save(const ControlState& state, u8* data, u32 capacity);  // bytes written, at most capacity (n)
void control_load(ControlState& state, const u8* data, u32 bytes);
```

When stock saves a project, the workspace included, `control_save` writes what
to keep; the SDK stores it in the project's [companion](projects.md). After
stock loads a project, `control_load` gets those bytes back, or `bytes` 0 when
the project has none (saved before, on stock, or without the machine) or is a
new one, so the machine can start empty. Stock loads and saves on its background
file task, not the GUI task. The SDK gives the data back on the next GUI tick,
after `control_reset` and before `control`, so the two never overlap.
`control_save` runs on the file task while stock saves and can overlap
`control`: keep data that `control` changes in single steps (such as stored
states), so that a save never catches a half-made change.

All machines' saved data together must fit the companion: the build refuses a
selection whose `savedBytes` add up to more than 16384.

A machine's control and display are compiled separately, so they share no
variables. The control can pass four words to its display each tick in
`overlay.display`, which the display reads with
`opxy::control::from_control(index)`: the words of the copy on the selected
track, 0 when it did not run, and always 0 in browser previews.

Track control marks what it does on every page, over stock's: while a machine
captures, its capture marks; with `overlay.mark` set to 1, its line marks on the
page of any track it overlays. The SDK's are a red frame fading inward at the
screen's edge and a thin dark red frame. The SDK draws them in the last screen
page, which no machine is given.

A machine can draw its own instead, in any colour or shape: it adds
`"marks": {"capture": c, "line": l}` to `trackControl` (1..16 capture nodes,
0..16 line nodes; 0 never marks steered tracks) and supplies
`marks.svg.fragment`, one `<g id="<id>_marks">` holding exactly
`<id>_capture_0`..`<id>_capture_<c-1>` and `<id>_line_0`..`<id>_line_<l-1>`,
each a `path` or `circle` in the authored SVG subset, without text. They are
drawn over every stock page, so keep them to the screen's edges or small, and
leave the page readable; the capture marks must make clear that edits are being
recorded. [track_control_overlay.svg](../sdk/platform/opxy-1.2.1/track_control_overlay.svg)
holds the SDK's, as a starting point.

### Aux tracks

A track control machine, unique and with its own display, runs on an aux
track (SDK 1.7): it lists it in `"aux"` (`[12]`, CV) in `trackControl`. That track is the
only one for now, the one whose stock role the SDK keeps silent. Such a machine
is not in tracks 1-8's engine list: it is chosen on its aux track only.

- **Choosing:** Shift + M1 on track 12 opens a list, "engine", with the
  track's stock role (`cv`) first, then each machine that may take it over:
  up to 31 in one build, which the build checks.
  As tracks 1-8 hold an engine, each pattern holds its own choice: it applies
  to the current pattern, is copied with it and saved with the project, and
  chained patterns switch between CV and the machine. While a machine holds the
  playing pattern, CV and gate stay silent. A new project starts with `cv`.
- **Its interface:** M1 shows the machine's display. Its encoders, clicks,
  fine turns and parameter locks use the machine's controls directly; CV's
  page dispatch does not run. M2/M3 are delivered as `page_key` actions and
  do not open stock pages.
- **Optional shared LFO:** `"sharedLfo": true` in `trackControl` gives M4 to the
  shared LFO page, naming destinations after the machine's first four
  parameters. By default M4 is another machine action, no stock page is
  exposed, and stock LFO modulation is excluded from the machine's controls.
  The shared LFO uses the track's existing LFO settings.
- **Its controls:** words 0..7 of track 12, so they are sequenced, locked and
  saved like any track parameter, and `values[11]` holds them. Taking a
  pattern over sets the machine's defaults; giving it back restores the role's
  values (a pattern copied from one the machine held takes another pattern's). A project opened with a build that lacks the machine gets the role's
  values back too. Opened on stock firmware, CV plays the machine's values.
- **Presets:** Shift + track key on track 12 lists presets in two columns, as
  stock's preset browser does: folders, then the presets in them that can run
  on the track. Choosing one gives the track to its machine (or role) and sets
  its controls. The builders install a machine's definition presets as
  `content/aux_presets/<id>/<name>.json` (names lowercased, at most 15
  characters), and `content/aux_presets/cv/init.json` so CV is listed too;
  users add their own as `aux_presets/<folder>/<name>.json` over MTP. Each is
  `{"params": [eight values 0..32767], "platform": "OP-XY", "type": "<machine
  id or cv>", "version": 1}`. Without `params`, a preset only gives the track
  to its machine or role, leaving the controls as they were, as CV's `init`
  does.

It runs on the GUI side like any track control machine; nothing changes on the
audio side but the silenced CV output.

## Sample engines

[sample_engine.hpp](../sdk/include/opxy/sample_engine.hpp) defines the sample
interface. A sample machine supplies `sample.cpp` with:

```cpp
struct Voice { ... };
void initialize(Voice&, const opxy::SampleView&);
void start(Voice&);
void render_sample(Voice&, const opxy::SampleBlock&);
```

`initialize` receives the read-only sample stock assigned to the voice, which
the SDK retains until the voice is destroyed; a view with no frames means
silence. `start` runs on each note. Never retain a block or its buffers.

The stock sampler a sample engine forks (`machine.json` `base`) sets how it
plays and which stock screen it uses. With `"base": "sampler"` it plays like
the stock sampler: one sample, played chromatically across the keyboard, its
root key playing it as recorded. With `"base": "drumSampler"` it plays like
the stock drum sampler: 24 pads, each with its own sample.

### Services

Each service is an optional entry in `definition.json` `resources`; omitted
means 0. Callbacks are functions in the machine's namespace.

| Resource | Value | Effect | Callbacks |
| --- | --- | --- | --- |
| `stereoOutput` | 0 or 1 | Output is 64 interleaved left/right frames | |
| `chokeSameKey` | 0 or 1 | A note silences earlier voices on the same track and key (drum sampler base: the same pad, at any octave) | |
| `padSettings` | 0 or 1; drum sampler base | The SDK saves 18 machine-defined bits with each pad | `sample_defaults`, `sample_assign` |
| `drumControls` | 0 or 1; needs `padSettings` | The drum page's machine encoders and panel | `drum_control`, `drum_panel` |
| `sliceTiming` | 0 or 1; needs `drumControls` and SDK 1.7 | Lazy-chop audition through the machine, with a matching source-frame tap clock | `slice_clock` |
| `sampleAnalysis` | Budget in bytes, 4096..2 MiB, whole words | Prepared per-sample data | `analysis_words`, `prepare_begin`, `prepare_step`, `prepared` |

### Audio block

| `SampleBlock` field | Contract |
| --- | --- |
| `output` | 64 Q31 words, or with `stereoOutput` 64 interleaved left/right frames (128 words). Write every word |
| `parameters` | The track's eight live Q31 parameters; the first are the declared controls |
| `pitch` | 64 phase increments, as for a synth |
| `transport` | Advisory project tempo, 96-PPQN tick and play state |
| `region.begin`, `region.end` | The pad's trim, a half-open frame interval within the sample; empty means silence. With `drumControls`, the outer markers |
| `region.level` | Unsigned Q27 gain; `opxy::sample_unity_level` is unity |
| `region.reverse` | The pad's saved direction |
| `region.pitch_cents` | The pad's saved tuning, in cents |
| `analysis` | This machine's completed analysis of the sample, or null while pending, declined or over budget |
| `settings` | The pad's saved settings, or `sample_defaults()` when it has none; 0 without `padSettings` |
| `output_channels` | 1, or 2 with `stereoOutput` |
| `track_transpose` | The track settings transpose, -36..36 semitones |
| `drum_transpose` | Drum sampler base: semitones from the note's pad to the played note (whole octaves), fixed at note start. 0 with the sampler base |
| `slicing` | A lazy-chop audition: traverse the supplied outer range once, forwards, using frozen settings, parameters and tempo. Ignore live trims, reverse and loop settings |

Source and output channel counts are independent. Stock envelopes, mixing and
project voice settings follow the callback.

### Slice timing

```cpp
void slice_clock(const opxy::SampleSlice&, opxy::SampleSliceRate&);
```

With `sliceTiming`, the GUI calls `slice_clock` with the loaded frame count and
file rate, outer range, pad settings, eight live parameters, track transpose and
project tempo captured when lazy chop starts. Return a nonnegative `numerator`
and positive `denominator` and `tuning`: source frames per 44.1 kHz output frame
are `numerator / denominator * tuning / 4096`. Account for the same timing and
pitch arithmetic as the audio renderer. This callback accesses no PCM or analysis.

The SDK sends the frozen configuration with the audition note. Audio receives
it in `SampleBlock` with `slicing=true`, transport stopped at tick zero, and
drum transpose zero. Subsequent cuts cannot change the audition range; control,
automation and master-tempo edits apply to the next audition. A renderer that
waits for analysis must keep the audition's source clock advancing while silent,
then resume at its elapsed position. Without this service, lazy chop uses one
forward pass of raw PCM at the original file rate. Ordinary notes use normal
machine playback.

### Pad settings

`PadSettings` is an unsigned value from 0 to 262143 (18 bits), stored in a
`u32` since SDK 1.6. The SDK reads existing 16-bit settings without changing
their values. Machines using values above 65535 must target SDK 1.6 or later;
firmware built with older SDKs cannot read those extended values.

```cpp
opxy::PadSettings sample_defaults();
opxy::PadSettings sample_assign(const opxy::SampleAssignment&);
```

`sample_defaults` returns the settings of a pad that has none. `sample_assign`
runs when a file or a completed stock recording (`recorded`) is assigned to a
pad and returns the pad's new settings. It receives the file path
(`filename_bytes` long, not NUL-terminated, possibly null; null for a
recording), the loaded frame count and rate, the project tempo, and in
`previous` the settings of the pad's replaced sample (`replacing`) or the
defaults. The new sample's trim spans its whole length; a replaced pad keeps
its pitch, reverse and volume. Settings survive project save exactly.

### Drum page

```cpp
void drum_control(opxy::PadSettings&, const opxy::DrumControl&);
void drum_panel(opxy::PadSettings, const opxy::SampleRegion& saved, opxy::Parameters live, opxy::DrumPanel&);
```

| Encoder | Main | Shift |
| --- | --- | --- |
| 1 | Pad pitch: ±12 semitones in ten-cent steps; press resets | Stock reverse |
| 2 | Stock start marker | Machine, knob 1 |
| 3 | Stock end marker | Machine, knob 2 |
| 4 | Machine, knob 3 | Stock volume |

`drum_control` receives each turn (signed direction) or press of a machine
encoder, with `shifted` set on the Shift row, and edits the settings. `held`
marks a turn while the encoder is held down; turning suppresses the click on
release. Machines decide how that gesture changes their stepping.
`drum_panel` receives the settings, the pad's saved region and the eight live
parameters audio uses, and reports the pad as the machine plays it: the live
pitch offset in cents (the page adds the pad's saved pitch), loop or play once,
direction, a Q31 scale of the saved volume, and two Shift captions of at most
15 characters, which replace the pan row. The panel arrives with pitch 0, the
saved direction and an unchanged level, so a machine sets what its controls
change. On the drum page `saved.level` is unity: stock keeps the pad's volume
separately, and `level` scales it. The SDK only projects the panel onto the
stock page, so a machine applies its live controls in one place for display
and audio. Callbacks are bounded and allocation-free.

Stock's page reads slots 0 to 3 as pitch, start, end and play mode. The SDK
replaces pitch and play mode with the panel's, so only slots 1 and 2, the start
and end trim overlays, reach stock directly; slot 0 keeps stock's ten-cent pitch
editing, -12..12 semitones. The machine defines the remaining slots in
`definition.json`. See [Timestretch](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/timestretch/controls.h).

### Sample analysis

```cpp
struct Preparation { ... };
opxy::u32 analysis_words(const opxy::SampleView&);
bool prepare_begin(Preparation&, const opxy::SampleView&, opxy::u32* storage, opxy::u32 words);
opxy::PrepareStatus prepare_step(Preparation&, opxy::u32 frames, opxy::u32& cursor);
bool prepared(const Preparation&, opxy::SampleAnalysis&);
```

[sample_analysis.hpp](../sdk/include/opxy/sample_analysis.hpp) defines the
types. After a file is assigned to a pad, the SDK asks each analysis machine how
many words the sample needs (0 declines), allocates them from the stock heap
within that machine's budget, and runs `prepare_step` in bounded chunks on the
stock file worker until `prepared` publishes the immutable result. Nothing is
prepared while the sample browser is open, and a sample that does not fit stays
unanalyzed. Results are released before stock frees the sample. A collection's
analysis budgets together may not exceed 2 MiB.

## Effects

[effect.hpp](../sdk/include/opxy/effect.hpp) defines the effect interface. An
effect supplies `fx.cpp`, which includes its `core.h`, with these callbacks in
its namespace:

```cpp
struct State { ... };
void initialize(State& state);
void process(State& state, opxy::Channel left, opxy::Channel right,
             opxy::Parameters parameters);
```

Stock constructs an effect in place in its send slot on the audio thread and
calls `initialize`, which runs again on an engine reset, so keep it short;
storage starts zeroed. `process` runs once per
64-frame block on the stereo send buffers and replaces them in place: `left`
and `right` are distinct 64-word Q31 buffers. `parameters` holds the stock
eight-word vector of normalized Q31 words; the first are the one to eight
declared controls. Do not depend on the absolute level of the send buffers.

`State` is at most `opxy::effect_state_bytes` (476) with alignment at most 4.
No destructor ever runs, so it holds no resources. The generated adapter
checks both limits at compile time. Effects use the stock FX page, with any controls after the fourth as
[alternates](#alternate-controls) or a [second page](#effect-pages-and-shift-rows), and have no browser preview.

### Effect pages and shift rows

An effect with five to eight controls may declare `"press": "page"` (SDK 1.7),
as a synth does: a press of any encoder on its send page switches all four
positions between its first four controls and the rest.

An effect may also declare a **shift row** (SDK 1.7): one to four enum
controls in `"shift"` in `definition.json`, which Shift and the send page's
four encoders choose, one choice per detent, while the page shows their names
and choices. They suit routing and modes, such as an order or a source. All
their choices together are at most 1,048,576 combinations; the first choice of
each is what a new project starts with. With a shift row, Shift no longer
turns the effect's continuous controls in fine steps.

`process` then receives twelve words: the eight controls, then each shift-row
control's chosen word, read as any enum control:

```json
"shift": [
  {"id": "order", "label": "Order", "default": 0, "display": "enum",
   "mapping": {"kind": "enum", "labels": ["TPES", "TEPS", "PTES"]}}
]
```

```cpp
const opxy::u32 order = ((static_cast<opxy::u32>(parameters[8]) >> 16) * 3) >> 15;
```

Its `State` is then 4 bytes smaller than otherwise (the slot keeps the send's
number). See the [firmware contract](abi-1.2.1.md#effect-pages-and-shift-rows).

### Effect pages

An effect may draw its own page on the send tracks (SDK 1.7): it declares
`resources.display` and supplies `display.h`, `display.cpp` and
`panel.svg.fragment`, exactly as a synth does ([display API](display.md)).
Each build's layout then gives it a page from 82..255 beside its effect
number, as it gives a synth its own. Stock shows the page an effect's
record names on M1 of a send, so the page replaces stock's FX page there; its
header names and captions the effect's controls, or its shift row's while
Shift is held, and the display reads them all, the shift row too
([input](display.md#effect-pages-sdk-17)). Effects have no browser preview
yet. An effect that comes to draw its own page keeps its effect number and is
given a page in its firmware's layout.

### Effect buffers

An effect that needs memory beyond its state, such as a delay line, declares
`"bufferBytes"` in `definition.json` `resources`: whole words, at most 524288
(512 KiB). It needs `"sdk": "1.2"` in `machine.json`, and its `process` takes
the buffer:

```cpp
void process(State& state, opxy::Buffer buffer, opxy::Channel left,
             opxy::Channel right, opxy::Parameters parameters);
```

`buffer.words` is the send slot's scratch memory, `buffer.count` words long;
stock delay and reverb use the same memory. Its `State` is at most
`opxy::effect_buffered_state_bytes` (472). The buffer:

- **Is never cleared:** it holds whatever the slot's previous effect, or this
  one before an engine reset, left. Read only what the effect has written since
  `initialize`, for example by counting the words written.
- **Is empty outside a send:** `buffer.words` is null and `buffer.count` 0;
  write silence then.
- **Is per send:** an effect on both sends gets two buffers.

### Tempo

An effect declares `"transport": 1` in `resources` (SDK 1.2) to receive the
audio clock's tempo, read once per block, after the buffer if it has one:

```cpp
void process(State& state, opxy::Buffer buffer, const opxy::Transport& transport,
             opxy::Channel left, opxy::Channel right, opxy::Parameters parameters);
```

`transport.bpm_x10` is the tempo in tenths of BPM, or 0 when there is none
(see [`opxy/transport.hpp`](../sdk/include/opxy/transport.hpp)). A control with
`"display": "division"` and a linear 0..1 mapping picks one of stock delay's
note lengths, an eighth of its travel each: 1/32, 1/32 dotted, 1/16, 1/16
dotted, 1/8, 1/8 dotted, 1/4 and 1/2. `opxy::division(control)` gives the index
and `opxy::division_sixteenths` the length in sixteenths of a quarter note, and
the send page shows the length's name in place of the number, as for stock
delay's Size.

`process` writes only the wet return. An effect may declare `dryParameter` in
`machine.json`, naming a control with a linear 0..1 mapping. The adapter returns
that control's raw Q31 word to the stock source mixer. Raising it restores dry
source level without reducing the wet return. For one isolated send with effective
send weight `s` and Dry `d`, the source dry factor is `1 - s*(1-d)`. At full send,
Dry zero removes the source dry and Dry one retains it; at zero send, dry stays
unchanged. Do not add clean audio inside `process` to implement this control.
Without `dryParameter`, the adapter returns unity to preserve the source dry.
Instead, an effect may fix its Dry with `dryLevel` in `machine.json` (SDK 1.7),
a number from 0 to 1. An effect that mixes its own dry, as a box of effects
inline does, declares 0: at full send a track then goes wholly through it.

## Registration boundary

A machine's ID (`definition.json`'s `id`) is its stock name: the engine type the device lists, its catalog and
preset entries, its screen file, its symbol prefix and its C++ namespace. It is a lowercase letter followed by letters, digits or `_`, at most
15 characters. It may not be a stock engine or effect type, a C++ keyword,
`opxy` or `std`, or start with a reserved prefix (`extensions`, `sdk`, `kit`,
`stock`, `local`).

`machine.json` declares how the SDK builds the machine:

| Field | Contract |
| --- | --- |
| `schema` | 1 |
| `sdk` | The SDK version the machine targets, `"<major>.<minor>"`. This SDK is 1.7 and builds 1.0 to 1.7 machines |
| `kind` | `synth`, `sample` or `effect` |
| `description` | Plain text of at most 160 characters, shown by the builder and preview |
| `base` | Sample engines only, and required for them: the stock sampler it forks, `"sampler"` or `"drumSampler"` ([sample engines](#sample-engines)) |
| `sources`, `includes` | Optional extra translation units and include folders inside the machine folder; synths only, since the native plan compiles one source for the other kinds |
| `icon` | Optional builder glyph: SVG path data in a 46 × 48 box, drawn as a stroke. The kind's glyph is used otherwise |
| `presetDefaults` | Optional `{"amp": {...}}` overriding generated factory preset envelope values: `attack`, `decay`, `release` and `sustain`, each 0..32767. It does not rewrite existing projects or presets |
| `presets` | Optional folder inside the machine folder holding the machine's own [presets](#presets); `sdk` 1.3, instruments only |

An effect's optional `dryParameter` field names the control used by the stock
source dry mixer, as described under [effects](#effects).

### Presets

The [builder](builder.md) installs an init preset for each instrument,
`content/presets/<id>/default.json`, from its control defaults and
`presetDefaults`. A machine can ship its own: save them on an OP-XY running
firmware with the machine, copy the preset files into the folder `presets`
names, and the builder installs each `<name>.json` as
`content/presets/<id>/<name>.json`. A `default.json` there replaces the
generated init preset. Presets store the machine's ID, so they work in any
firmware that holds it.

- A name is lowercase letters, digits, `-` and `_`, at most 24. Other files in
  the folder, except `.md` and hidden files, are refused.
- At most 16 presets, each at most 16 KiB.
- Each is a version 4 OP-XY preset whose `type` is the machine's ID, with
  eight engine `params` from 0 to 32767 and no sample `regions`, since a
  preset never points into a user's sample library. Other fields stay as the
  device saved them.

### Identity

A machine is identified by its `definition.json` `id`. Presets save that name,
so it never changes once published. `machine.json` holds no numbers: each
build gives every machine the stock numbers projects save, an engine number
(an effect number for an effect) and a screen number, and keeps them.

| Kind | Engine or effect number | Screen |
| --- | --- | --- |
| `synth` | Engine 34..255 | Its own, 82..255 |
| `sample` | Engine 34..255 | Its base's: the stock sampler's or drum sampler's |
| `effect` | Effect 1, 8, 11, 21, 23..28 or 34..62 | The stock FX screen, or its own, 82..255 (SDK 1.7) |

These numbers exist only in the relocated stock tables, so no machine shares
one with stock. Four core machines, which the first Open-XY firmware shipped,
have fixed numbers held in every firmware whether it includes them or not
(`FIXED_NUMBERS` in [`tooling/machines.py`](../tooling/machines.py)).
Every other machine is numbered by the firmware's **layout**: its record of
every number it has given out, including to machines since dropped, which stay
held so an old project never opens another machine. The builder and
`sdk.py build` write the layout after each build to `layout.json`; give it to
the next build so every machine keeps its numbers. A machine new to the layout
gets numbers from its own place in each pool above, worked out from its ID
(FNV-1a, 32-bit, of the ID's UTF-8 bytes, modulo the pool's size), or the next
free number above it, wrapping around. Machines are numbered in ID order, never
collection or selection order, so even a build without a layout gives a machine
the same numbers whatever else it includes, unless another machine's place
collides with its own: then the first by ID keeps it. Only a layout guarantees
numbers.
A [project companion](projects.md) records what a project's numbers meant by
machine ID, so Open-XY firmware with another layout opens the project with the
same machines.

Machines targeting SDK 1.4 or earlier instead list the numbers they ask for in
`identity`, `{"engine": n, "page": p}` or `{"effect": n, "page": 21}`, and
name no `base`. A layout gives them when free, before any machine's own place,
otherwise numbers as for any other machine;
a sample engine's `page` 2 forks the sampler and 7 the drum sampler. Such a
machine keeps working unchanged. To move it to `"sdk": "1.5"`, delete
`identity` and, for a sample engine, add its `base`; a 1.5 machine with
`identity` is refused. Its numbers in an existing `layout.json` stay its own,
so build with the previous layout.

Generated adapters check record/name ownership and screen readiness before
catalog admission; the packager authenticates firmware and constrains writes.
See [native builds](build.md). Generated instrument presets use polyphonic
voice mode.

Arbitrary external-engine loading is not implemented: a machine reaches the
firmware through the collection, from the [catalog](catalog.md) or as a folder
passed to a build with `--machine`.
