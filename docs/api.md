# Engine API

A machine is a folder under `machines/` named after the machine. Every kind has
`machine.json` and `definition.json`; the C++ it supplies depends on its kind:

| Kind | C++ the machine supplies | Example |
| --- | --- | --- |
| `synth` | `engine.cpp` with `Voice`/`Shared` state, `display.cpp` and `panel.svg.fragment` | [2op FM](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/fm2op/engine.cpp), [Macro Osc](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/engine.cpp) |
| `sample` | `sample.cpp` with `Voice` state; the stock sample page is its screen | [Timestretch](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/timestretch/sample.cpp) |
| `effect` | `fx.cpp` with `State`; the stock FX page is its screen | [Ring Mod](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/ringmod/fx.cpp) |
| `step` | `step.cpp` with per-track state and its callbacks; it replaces a stock [step component](#step-components) | `python sdk.py new --kind step` (Transpose) |

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
| Step components | Replace one stock step component; ten settings; at most 256 bytes of state per track |
| Artwork | At most four decimal places and no exponent notation in any number; explicit paint on every leaf; no `style`, events or external references |

The [firmware contract](abi-1.2.1.md) explains where each rule comes from.

## Memory and limits

Every selected machine and the SDK share **one 2040 KiB module region**: code,
constant data and zero-initialized state together. It is the 2 MiB the
firmware reserves from its heap, minus 8 KiB of relocated stock tables. There
is no per-machine allowance: a build that does not fit names each machine's
share, and `sdk.py compile` and the builder's memory meter report the same
figures. State usually dominates, since a synth's voice pool is
`voiceBytes × (min(voiceCapacity, 27) + 1)`: stock never sounds more than 27
notes, and one spare voice serves a note that ended as another starts. The SDK
adds 12 bytes per stock voice index (3 KiB) and 16 per voice.

| Limit | Value | Why |
| --- | --- | --- |
| Module memory | 2040 KiB for the whole collection | Firmware heap reservation minus the relocated tables |
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
  [`q31.hpp`](../sdk/include/opxy/q31.hpp) has the fixed-point helpers.
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
per track, starts on the position's own parameter and is not saved. Display
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

The stock page an engine asks for (`identity.page`) sets how it plays. On page
2, the sampler's page, it plays like the stock sampler: one sample, played
chromatically across the keyboard, its root key playing it as recorded. On
page 7, the drum page, it plays like the stock drum engine: 24 pads, each with
its own sample.

### Services

Each service is an optional entry in `definition.json` `resources`; omitted
means 0. Callbacks are functions in the machine's namespace.

| Resource | Value | Effect | Callbacks |
| --- | --- | --- | --- |
| `stereoOutput` | 0 or 1 | Output is 64 interleaved left/right frames | |
| `chokeSameKey` | 0 or 1 | A note silences earlier voices on the same track and key (page 7: the same pad, at any octave) | |
| `padSettings` | 0 or 1; page 7 | The SDK saves 16 machine-defined bits with each pad | `sample_defaults`, `sample_assign` |
| `drumControls` | 0 or 1; needs `padSettings` | The drum page's machine encoders and panel | `drum_control`, `drum_panel` |
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
| `drum_transpose` | Page 7: semitones from the note's pad to the played note (whole octaves), fixed at note start. 0 on page 2 |

Source and output channel counts are independent. Stock envelopes, mixing and
project voice settings follow the callback.

### Pad settings

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
[alternates](#alternate-controls), and have no browser preview.

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

## Step components

A step component changes what a sequencer step plays: its notes, how many
times it repeats or ratchets, whether it sounds, and where the playhead goes
next. It takes over one of stock's step components: holding shift, a step and
that component's white key adds it to the step, and the black keys choose one
of its ten settings
([TE's guide](https://teenage.engineering/guides/op-xy/step-components)).
[step.hpp](../sdk/include/opxy/step.hpp) defines the interface (SDK 1.5). The
machine's `step.cpp` holds per-track state and the callbacks its definition
lists, in its namespace:

```cpp
struct Track { ... };   // one per track, zeroed at every reset, at most 256 bytes
void reset(Track& track);
bool condition(Track& track);
void enter(Track& track, const opxy::StepContext& at, opxy::u32 setting);
opxy::u32 trigs(Track& track, opxy::u32 setting);
bool mute(Track& track, const opxy::StepContext& at, opxy::u32 setting);
void apply(Track& track, const opxy::StepContext& at, opxy::StepNote& note, opxy::u32 setting);
bool next(Track& track, const opxy::StepContext& at, opxy::u32 setting, opxy::StepNext& next);
bool plays(Track& track, const opxy::StepContext& at, opxy::u32 setting);
```

Stock calls them for a step that has the component:

| Callback | When | Does |
| --- | --- | --- |
| `reset` | Sequencer start, for every track; a track restart | After the adapter zeroes `Track` |
| `enter` | The playhead enters the step, including repeats and jumps | Stock bend starts its curve; multiply picks a random count |
| `trigs` | The step's trig | 2..8 evenly spaced trigs (stock spaces them); 0 or 1 is one trig |
| `mute` | The step's trig | `true` plays no trig this pass |
| `apply` | Every note the step plays | Changes `note.pitch`, `note.velocity` and `note.length` |
| `next` | The playhead leaves the step | `next.step` arrives as the natural next step; `true` goes to `next.step` instead |
| `condition` | Before the step's "skip component" condition | `false` ignores the condition (stock pulse and hold do while repeating) |
| `plays` | On a condition key only: before the step's trig (skip trig), parameter locks (skip param) or other components (skip comp) | `false` skips them; without `plays` they always happen |

An unlisted callback does what an empty stock component does: nothing, one
trig, no mute, no jump, the condition applies, and nothing is skipped. `setting` is the black
key's position: black keys 1 to 9, then 0. `at` gives the track (0..15), the
step, the pattern's length and the note-length ticks one step spans at the
track's speed (480 at 1/1). `note.pitch` is a MIDI note; after every component
has run, stock folds it into 0..127 by octaves. The adapter clamps
`note.velocity` to 0..127, and velocity 0 does not mute (use `mute`).
`note.length` is the note's remaining length in ticks, or -1 while a key
holds it. In `next`, clear `next.counts` on a repeat so "every Nth" conditions
don't count the pass; components answer in stock's order (hold, pulse,
multiply, velocity, the ramps, random, portamento, bend, tonality, jump) and
the first `true` wins. On the three condition keys, stock's picker stores
black key 2 as setting 2, like key 3, so a component there sees nine distinct
settings. The callbacks run in the sequencer's real-time path:
keep them short, with no allocation, blocking or I/O.

Stock's own components fit this shape: velocity, random, portamento and
tonality are `apply`; bend is `enter`; multiply is `enter` and `trigs`; pulse
is `reset`, `condition` and `next` (stay on the step until its count);
hold is pulse plus `mute` for the replays and `apply` lengthening the first
note; the ramps are `reset` and `apply` counting notes; jump is `next`; and the
three conditions are `plays`, counting the step's visits. Notes
always start at their recorded time, so no callback moves a note in time, and
none adds notes.

`definition.json` holds the component's `name`, at most ten letters, digits or
spaces, which the picker shows in lower case; its ten `settings` in black-key
order, each two lines of at most 14 lower-case letters, digits, spaces or
`% + . / -`, as the picker shows them; and the `callbacks` its `step.cpp`
defines:

```json
{"schema": 1, "id": "transpose", "name": "Transpose", "callbacks": ["apply"],
 "settings": [["octave", "down"], ["fifth", "down"], ...]}
```

`machine.json`'s `identity` names the stock component it replaces,
`{"step": "velocity"}`; a firmware's [layout](#identity) can move it, and the
[builder](builder.md) lets people place it on any key: `pulse`, `hold`,
`multiply`, `velocity`, `ramp_up`, `ramp_down`, `random`, `portamento`, `bend`,
`tonality`, `jump`, `skip_param`, `skip_component` and `skip_trig`. On a
condition key every callback works as on the others, and `plays` decides what
the key skips. A replaced component keeps its key's printed icon; the picker shows the
machine's name and setting labels, and all of the stock component's behaviour
is gone. Projects store each step's setting numbers by component, so a project
made with another component on that key plays the same settings through this one.

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
| `sdk` | The SDK version the machine targets, `"<major>.<minor>"`. This SDK is 1.5 and builds 1.0 to 1.5 machines |
| `kind` | `synth`, `sample`, `effect` or `step` |
| `description` | Plain text of at most 160 characters, shown by the builder and preview |
| `identity` | The numbers it asks for: `{"engine": n, "page": p}`, or `{"effect": n, "page": 21}` for an effect; see below. A step component names the stock component it replaces, `{"step": "velocity"}` |
| `sources`, `includes` | Optional extra translation units and include folders inside the machine folder; synths only, since the native plan compiles one source for the other kinds |
| `icon` | Optional builder glyph: SVG path data in a 46 × 48 box, drawn as a stroke. The kind's glyph is used otherwise |
| `presetDefaults` | Optional `{"amp": {...}}` overriding generated factory preset envelope values: `attack`, `decay`, `release` and `sustain`, each 0..32767. It does not rewrite existing projects or presets |
| `presets` | Optional folder inside the machine folder holding the machine's own [presets](#presets); `sdk` 1.3, instruments only |

Step components have no `sources`, `includes`, `presets`, `presetDefaults` or numbers.

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

| Kind | ID | Page |
| --- | --- | --- |
| `synth` | 34..62 | Its own screen: 82..127 |
| `sample` | 34..62 | Stock sample page 2 or 7; `padSettings` requires 7 |
| `effect` | 1, 8, 11, 21, 23..28 or 34..62 | Stock FX page 21 |
| `step` | None: it names a replaceable stock component | None |

Engine IDs above 32, effect IDs above 18 and pages above 81 exist only in the
relocated stock tables.
Presets save a machine's name and projects its numbers, so a name never changes
once published, and neither do a firmware's numbers.

`identity` holds the numbers a machine asks for. Four core machines, which
the first Open-XY firmware shipped, have fixed numbers held in every firmware
whether it includes them or not: 2op FM 34/82, Macro Osc 26/83, Ring Mod
effect 1 and Timestretch 35 on sample page 7 (`FIXED_NUMBERS` in
[`tooling/machines.py`](../tooling/machines.py)). No other machine gets fixed
numbers, since a layout may already hold any free number.
Every other machine is numbered by the firmware's **layout**: its record of
every number it has given out, including to machines since dropped, which stay
held so an old project never opens another machine. The builder and
`sdk.py build` write the layout after each build to `layout.json`; give it to
the next build so every machine keeps its numbers. A machine new to the layout
gets the numbers it asks for when they are free, otherwise the lowest free
ones. New numbers come from engines 34..62 and pages 82..127, never from the
gaps in stock's own engine table, so ask for numbers in those ranges; a
sample engine keeps its stock page. Macro Osc predates this and keeps
engine 26.

The layout also records the stock component each step component replaces,
`{"step": "velocity"}`. A step component gets the layout's component while it
is still replaceable and free, otherwise the one it asks for, otherwise the
first free one. Unlike numbers, a dropped step component holds nothing: its
entry stays only as a hint for when it is chosen again.
A [project companion](projects.md) records what a project's numbers meant by
machine ID, for firmware with another layout.

Generated adapters check record/name ownership and screen readiness before
catalog admission; the packager authenticates firmware and constrains writes.
See [native builds](build.md). Generated instrument presets use polyphonic
voice mode.

Arbitrary external-engine loading is not implemented: a machine reaches the
firmware through the collection, from the [catalog](catalog.md) or as a folder
passed to a build with `--machine`.
