# Engine API

A machine is a folder under `machines/` named after the machine. Every kind has
`machine.json` and `definition.json`; the C++ it supplies depends on its kind:

| Kind | C++ the machine supplies | Example |
| --- | --- | --- |
| `synth` | `engine.cpp` with `Voice`/`Shared` state, `display.cpp` and `panel.svg.fragment`; `control.cpp` with [track control](#track-control) (SDK 1.9) | [2op FM](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/fm2op/engine.cpp), [Macro Osc](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/macroosc/engine.cpp) |
| `drum` | The synth callbacks and drawing display, plus per-key model/control callbacks ([synthesis drum kits](#synthesis-drum-kits)), from SDK 1.8 | Independent model selection on 24 keys |
| `sample` | `sample.cpp` with `Voice` state; the stock sampler it forks provides its screen, or from SDK 1.8, forking the sampler, optionally its own (`display.cpp` and `panel.svg.fragment`) | [Timestretch](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/timestretch/sample.cpp) |
| `effect` | `fx.cpp` with `State`; the stock FX page is its screen, or its own (SDK 1.7) | [Ring Mod](https://github.com/etcetc000/open-xy-core-machines/blob/main/machines/ringmod/fx.cpp) |

The [first-machine walkthrough](first-machine.md) creates a standalone synth
and previews it without native integration. SDK adapters own allocation,
C linkage and stock registration.

## Platform rules

These come from the stock firmware, not from SDK convention. Breaking one
fails the build or, for the artwork rule, halts the device's SVG parser.

| Area | Rule |
| --- | --- |
| Machine folder | C++ source, metadata, artwork, engine-specific generators and sound tests |
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
| Display shape | ≤ 4 paths of ≤ 64 points, ≤ 512 nodes (more than 256 need `"sdk": "1.8"`), ≤ 16 text slots (more than 4 need `"sdk": "1.3"`), ≤ 16 palette entries | Fixed-size renderer object and frame |

`voiceBytes`, `sharedBytes` and display state have no limit of their own
beyond whole words and module memory. Browser previews run 8 voices in
2 MiB of fixed WebAssembly memory, with growth disabled. This browser allowance
does not change the native collection's module limit or measure its footprint.
A drum preview auditions one parameter set across its voices; the device provides per-key drum settings.

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
- **64-bit arithmetic** and floating point may need software helpers. The
  build links only the required authenticated [arithmetic runtime](toolchain.md)
  members; their code counts toward module memory.
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
| `parameters` | Ordered controls, each with `id`, `label`, `default`, `mapping`, `display`: four to eight for a synth (see [alternate controls](#alternate-controls)), exactly eight for a drum kit, one to eight for a sample engine or effect; an effect's after the fourth are alternates on its send page. Labels are at most eight letters, digits or spaces |
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

A synth or drum kit for which a stock instrument page means nothing (one that makes no
sound of its own, say) may declare `"hidePages"` at the top of
`definition.json` (SDK 1.9), listing `"envelope"` (M2), `"filter"` (M3) or both. A
drum kit's M3 is its [track filter](#synthesis-drum-kits), shown unless hidden. On its
track, those keys then leave the page shown as it was, and selecting the track
while another track shows a hidden page shows the engine page. The SDK
corrects the page once per GUI tick, so the hidden page may flash for a frame.
The LFO page (M4) always stays on instrument tracks. Aux replacements own their
pages and receive their page keys directly; see [aux tracks](#aux-tracks).

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
| `trackControl` | Optional: the [track control](#track-control) state's `stateBytes` and `stateAlignment`, and optionally `unique`, `savedBytes`, `aux` and `sharedLfo` |

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

A synth can steer other tracks (SDK 1.9). A build without such a machine (or
one with [hidden pages](#alternate-controls)) applies none of track control's
firmware edits and links none of its code. Such a synth runs on an
[aux track](#aux-tracks), never in an instrument track's place. It declares
`"trackControl": {"stateBytes": n, "stateAlignment": a, "unique": true, "aux": [12]}`
in `resources`, with `"sdk": "1.9"` in `machine.json`, and supplies
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
its first call and whenever it arrives on a track.

| `Input` field | Contract |
| --- | --- |
| `track` | The aux track the machine holds, zero-based |
| `values[track][index]` | Every track's 58 live parameter words (`all_tracks`, 16; aux tracks are 8..15), as stock sets them (edits, parameter locks, sequencing), without any machine's overlay. Indices 0..7 are the engine's controls; the rest are stock's own track parameters, in stock's units. The machine's own engine controls are the values its page shows, including its LFO |
| `elapsed_us` | Microseconds since the previous tick, from the core clock: 0 on the first tick after a reset, at most one second |
| `transport` | The project's tempo in tenths of BPM (0 when unknown), the song position in 96ths of a quarter note, which advances only while playing, and whether the sequencer plays |
| `page_key` | Last directly handled page key since the last tick: 1..4 for M1..M4, or 0. M1 returns to the machine page; M2/M3 leave the current page alone. With `sharedLfo: true`, M4 belongs to the shared LFO and is not delivered |
| `muted` | 1 while the machine's track is muted in the current scene (aux + track key, or the mixer). As with stock's brain, a muted machine has no effect: none of its overlays apply. It still runs, so its page can say so |

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

A step's parameter lock plays as written: while a lock holds a parameter, no
overlay is written over it. The lock ends when stock sets the parameter to
something else, a step (24 ticks) after it was written if the track then plays
its own value, or when the sequencer stops; the next tick overlays it again. Loading or creating a project discards outstanding overlays.
Like the display, `control` is bounded
and allocation-free; it is not an audio callback. Browser previews do not run
it.

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
new one, so the machine can start empty. Both run on the GUI side. Such a
machine's `control_reset` runs just before each `control_load`, and when it
arrives on a track only if no load has run since startup: its state is the
project's, through every pattern change.

A machine's control and display are compiled separately, so they share no
variables. The control can pass four words to its display each tick in
`overlay.display`, which the display reads with
`opxy::control::from_control(index)`: the words of the copy on the selected
track, 0 when it did not run, and always 0 in browser previews.

A machine draws only on its own page: track control paints nothing over other
tracks' pages.

### Reading and setting other tracks

A track control machine reaches instrument tracks 1-8 (indices 0..7) three ways:

| To | Use | Lasts |
| --- | --- | --- |
| Get a value | `input.values[track][index]`: the live word, as stock sets it, without any overlay | Read each tick |
| Set it live | `opxy::control::add(overlay, track, index, value)` | This tick only: stock's value comes back when the machine stops adding it, and the project never saves it |
| Set it stored | `opxy::control::store(overlay, track, index, value)` | Until something else sets it: the track's current pattern keeps it, the project saves it and the page shows it, as an encoder edit would |

`store` returns false, and writes nothing, for a track outside 0..7, an index
outside 0..57 or once this tick holds `store_capacity` (16) stored writes. The
SDK applies them in order after `control` returns: it sets the stored value,
then calls stock's setter that an encoder edit ends in, which publishes it, so
`values` holds it from the next tick. It ignores a write to the machine's own
track, and every write while the machine's track is muted; negative values count
as 0. Beneath an overlay of the same parameter, the overlay still plays, and its
release restores the stored value. A step's parameter lock on it plays on as
written; stock's next step plays the stored value. Use `store` for changes the user should
keep (recalling a state), and overlays for movement (a morph or modulation):
each stored write changes the project, as an edit does.

Indices 0..7 are the engine's eight controls, the rest stock's own track
parameters. The SDK does not tell a machine a parameter's label, range or type:
the engine's controls are 0..32767 for whatever the engine on that track names
them, and stock's own words are in stock's units.

### Aux tracks

A track control machine, unique and with its own display, runs on an aux
track (SDK 1.9): it lists it in `"aux"` (`[12]`, CV) in `trackControl`. That track is the
only one for now, the one whose stock role the SDK keeps silent. Such a machine
is not in tracks 1-8's engine list: it is chosen on its aux track only.

- **Choosing:** Shift + M1 on track 12 opens a list, "engine", with the
  track's stock role (`cv`) first, then each machine that may take it over.
  As tracks 1-8 hold an engine, each pattern holds its own choice: it applies
  to the current pattern, is copied with it and saved with the project, and
  chained patterns switch between CV and the machine. While a machine holds the
  playing pattern, CV and gate stay silent. A new project starts with `cv`.
- **Its interface:** M1 shows the machine's display. Its encoders, clicks,
  fine turns and parameter locks use the machine's controls directly; CV's
  page dispatch does not run. M2/M3 are delivered as `page_key` actions and
  do not open stock pages. `hidePages` is unnecessary for an aux replacement.
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

## Synthesis drum kits

SDK 1.8 adds `"kind": "drum"`: one registered instrument with independent
model selection and sound controls on 24 keys. It supplies the synth
lifecycle, a drawing display, and the same resources plus `modelCount`
(2..64). Each key holds a model and eight 7-bit controls. A kit of more than
16 models stores them in a prefix code, which holds at most 15 models that use
all eight controls (`parameter_count` 8); `sdk.py test --machine` counts them
and fails a kit with more; on the device such a kit's keys play their defaults
and turn nothing. The definition declares
the eight controls: slots 0..3 on the four encoders and slots 4..7 their
second row, one per encoder. As a synth's two pages (`"press": "page"`), any
press switches the whole row between slots 0..3 and 4..7, and a slot the key's
model leaves unnamed shows blank; a drum kit declares no `press`. As on stock Drumline,
Shift and the first encoder choose the selected key's model, and Shift and
the second encoder its choke group, NONE or 1..3; keys sharing a group cut
each other off. While Shift is held the first encoder's cell shows MACHINE and
the model, the second's CHOKE and the group, and the third and fourth are
blank and turn nothing.

A kit may name groups of consecutive models in `resources.modelGroups`, such
as `[{"label": "TRX", "count": 13}, {"label": "EFM", "count": 8}]`: 2..16
groups, each with a label of at most eight letters, digits, spaces or hyphens
and at least one model, whose counts add up
to `modelCount`. Shift's first encoder then chooses the group (KIT), keeping
the model's place in it, so a kit's first model stays first; the second the
model within the group (MACHINE); the third the choke group (CHOKE); and the
fourth turns nothing.

With `"padMix": 1` in `resources` (SDK 1.10; 0 or 1, omitted means 0), Shift
gives each key the settings of a mixer channel. On the encoders' first row it
chooses the model (MACHINE: every model in turn; in a kit with groups the
cell's title is the model's group, `TRX` over `BD`), the choke group (CHOKE), and the key's own pan (PAN)
and level (LEVEL), as on stock Drumline: pan -100..100 with 0 centred, moving
faster on a fast turn as stock's does, and level in whole dB, -30..0, one a
detent, shown as `-6dB`. Pan and level are stock's own pad fields. Stock pans
every voice of a kit already; the SDK applies the level, as a cut, so a voice
stays within its declared `outputPeakQ31`. On the encoders' second row Shift
edits the key's pad fields: the kit's own pad controls on the first two
encoders, then its FX I and FX II sends. Every key of such a kit has the
second row, a model with four or fewer controls too, whose encoders there are
blank. Shift always edits the row the encoders are on; the display's `line`
says which. A press of any encoder switches the row with Shift held too, so
Shift and a press reach the second row without letting go.

Each key of a `padMix` kit has its own FX I and FX II sends, as a Machinedrum
track has its own delay and reverb sends: a kick can stay dry while the snare
is wet, even with the track's send knobs at 0. A send at 0 (`TRK`) follows the
track's send exactly; 1..7 send the key's own amount, shown as 0, 17, 33, 50,
67, 83 or 100, whatever the track's send knob says: a key at N sends what the
track's knob at N would, through the same curve stock gives that send's knob.
An amount scales the voice after stock's pan, level and envelope, and the
track's level and mute still apply to it. The track's own mix is unchanged,
and so is the source dry reduction of a send effect with a Dry control. The
kit's DSP never sees the sends ([firmware](abi-1.2.1.md#per-key-sends)).

A key plays its level, pad controls and sends once the kit has set it up: the
kit's first edit of a key does that, with its level at 0 dB and every pad field
at 0, so the key sounds as it did. Until then a key plays at 0 dB with every
pad field neutral, whatever stock or an earlier SDK build left in its pad
record, so projects saved before SDK 1.10, and keys a stock kit filled, play as
before. Projects keep a key's settings, and copying a pad copies them. A
preset keeps all of a key's settings too: loading one on a `padMix` kit sets
up every key the preset stores (its regions marked `fm slap`), with the level,
pad controls and sends the preset holds (a region's `gain`, and its `random`,
the pad word's top half: pad controls from bit 0, FX I's code at bits 9..11 and
FX II's at 12..14) ([firmware](abi-1.2.1.md#synthesis-drum-kits)).

A `padMix` kit may declare up to two small settings of each key's own in
`resources.padControls` (SDK 1.10), such as a filter and a sample-rate
reduction: a list of `{"label": "FILTER", "bits": 5, "signed": true}`, each with
a label of at most eight letters, digits, spaces or hyphens, and 1..7 bits, or
2..8 when `signed` (two's complement, so the field runs -16..15 in five bits),
together at most 9 bits. A value always fits the display's signed byte. They
step one a detent. 0 is neutral in every field, and a key the kit has not set
up reads 0. The pad fields are kept in a stock pad word that stock saves with
the project and copies with a pad. Before every render the adapter passes the
played key's pad controls to

```cpp
void drum_pad(Voice&, Shared&, opxy::u32 owner, const opxy::i32* values);   // optional; one per declared field
bool pad_control_text(opxy::u32 field, opxy::i32 value, char* text);        // optional; at most 15 characters
```

in the machine namespace; `pad_control_text` captions a value for the stock
header (without it, or when it returns false, the number shows). The display's
`pad` holds the selected key's pad fields. A kit's own header captions them
itself.

Every kit's track runs stock's own track filter on its voices, as a synth
track does (SDK 1.10). M3 is the filter, with stock's types (svf, ladder, z
lowpass, z hipass and the rest), cutoff, resonance, envelope amount and key
tracking, and its second row the track's level terms and FX I and FX II. Each
voice gets its own filter and its own filter envelope (M2 with Shift: attack,
decay, sustain, release), from the moment it starts, after the engine and
before the track's amplitude envelope and pan, so a key's own tone (a FILTER pad
control, say) and the track's filter act in series. M3's controls lock per step
through stock's editor, as on a synth, and act on every voice the track is
sounding during the step. With M3 switched off, as a kit's init preset has it
(stock's `fx`, `active: false`), the kit sounds exactly as without the filter
and costs one test a voice; switched on it costs about 1,300 to 2,600
instructions a voice a block depending on the type (the ladder and svf most),
and a kit's profiling workload runs the ladder on every voice so its catalog
figure includes it ([firmware](abi-1.2.1.md#the-track-filter-on-kit-voices)).
A resonant peak more than 6 dB above a kit's half-scale ceiling clips.

A `padMix` kit's steps lock its keys' PAN, LEVEL, pad controls, FX I and FX II
per hit and **per key**, as stock's steps lock a track's parameters. Hold a step
and Shift and turn one of them: the step gets a lock of that field for the key
the Shift row shows (the key the encoders edit: one held or selected with
Shift, else the selected key), one unit a detent (PAN faster when turned fast,
as without a step held). PAN (-100..100),
LEVEL (-30..30 dB) and a pad control (as far as its whole span, at most 127)
lock an offset to the key's own value, clamped to the field (LEVEL stays a cut,
-30..0); FX I and FX II lock the send itself, TRK or 0..100, starting from what
the key sends. A field turned back to 0 is unlocked. Each note the step plays
gets its own key's locks from the moment it starts: it keeps them for as long as
it sounds, and voices still sounding from earlier hits keep theirs. Other keys
on the same step play their own locks, or none: select each key to lock it.
While a step is held, the Shift row (and the display's `pan`, `level` and `pad`)
shows what that step plays for the selected key; released, the key's own values.
MACHINE and CHOKE turn nothing while a step is held: the model and the choke
group belong to the key. Without a held step Shift edits the key's own values as
above. A note played live gets no step's locks, only the key's own values.

The locks behave as stock's: copying and pasting a step or a pattern, page
duplication, nudge, clearing a step or its locks and the take's undo all carry
them, and they are saved with the project. A step keeps a handle to them in a
lock word of its own (37); the values live in an SDK pool kept in the project's
companion, `open-xy.locks` ([projects](projects.md#open-xylocks)), behind a
storage interface (`drum_lock_store.hpp`). A pasted copy is a copy: editing it
leaves the original. Limits and what is not kept:

- A project holds 2,457 locked steps with one field for one key (24 KiB of
  values; a step with four fields takes 16 bytes). Once the pool is full a turn
  locks nothing more; unlocking makes room again.
- The values are saved in the companion, before track control's state, which
  gets the room the locks leave.
- Saved on firmware without these locks (stock, or a build without a `padMix`
  kit), a project keeps its steps' handles but not their values, which play
  nothing from then on. Opened there and not saved, it keeps both.
- Turning these fields while recording records nothing; lock them by hand.
- With slide on, these locks don't slide: each hit plays its step's own.
- Synths have no such locks.

The engine's eight controls lock as before
([firmware](abi-1.2.1.md#step-locks-of-a-kits-shift-fields)).

In the machine namespace, declare these callbacks in `core.h` and implement
them alongside the engine:

```cpp
void drum_defaults(opxy::u32 key, opxy::u8* model, opxy::u8* controls); // a model and eight values
void drum_select(Voice&, Shared&, opxy::u32 owner, opxy::u32 model);     // before every render
const char* model_name(opxy::u32 model);
const char* parameter_name(opxy::u32 model, opxy::u32 slot);
opxy::u32 parameter_count(opxy::u32 model); // 0..8: slots 0 to count - 1 are used
```

Keys passed to `drum_defaults` are canonical keys 53..76. Controls are
integers 0..127; the model must be below `modelCount`. In a kit of more than 16
models, a key on a model of seven or fewer controls stores its first seven, so
changing a key's model keeps every control the new model stores and its eighth
reads the key's default. Keys are read in their kit's current layout: a kit that
grows past 16 models does not carry over the keys it saved before. Names are static
strings; parameter labels fit eight characters. Return an empty label for
unused slots. The screen shows the selected key's controls and labels with
the track's offsets applied, so a held step shows the values its locks play;
the display's `Input::model` is the selected key's model. A slot is available
when it is below the count and named: an unnamed slot shows blank and ignores
turns. Integer edits already have one-unit precision, including Shift turns.

The adapter stores the model and the eight controls losslessly in four
existing per-key preset words (eight 7 bits, the model 4), and marks the pad
with Drumline type 7 in its model byte, keeping its choke group unless one is
chosen. Unmarked
pads, including those stored by earlier 1.3 builds' eight-value layout, use
`drum_defaults`. The track's eight parameters are offsets to the eight
controls, neutral at Q15 16384, so step locks edit controls; the model is not
lockable. The adapter decodes the key's values, adds the offsets and clamps,
calls `drum_select` with the model, then `render` with each control as
`raw << 24`, 127 mapped to `0x7fffffff`. Packed words never reach machine
DSP. Presets and projects retain per-key settings using stock region storage.

With `"chokeSameKey": 1` in `resources` (0 or 1; omitted means 0), a kit
plays one voice per pad and track, as a Machinedrum track does: a note cuts the
kit's other voices still sounding the same track's same key, at any octave
(the same pad). Each cut voice keeps rendering while it fades out linearly over
128 frames (2.9 ms), then is destroyed and its note retired; its own handle
retriggered keeps its voice and does not fade. When a note needs a voice and
none is free, a voice already fading goes before the oldest note's. Without the
flag, notes on one key layer, up to `voiceCapacity`. This is separate from the
choke groups Shift chooses, which stock applies to keys sharing a group. A
synth or an effect cannot declare it; sample engines have their own
`chokeSameKey` (see [Services](#services)).

A kit's engine may also report that a voice's sound has ended, so stock
retires its note and the voice is free for the next one before stock's
amplitude envelope ends:

```cpp
bool finished(const Voice& voice, const Shared& shared);   // optional; in the machine namespace
```

The adapter asks after each rendered block. Once it returns true, the adapter
calls `destroy` and frees the voice, and stock retires the note after mixing
the block just rendered, so return true only once the voice renders silence.
The function must be in the namespace that defines `Voice`, where
argument-dependent lookup finds it, and take the voice and the shared state;
one elsewhere or with other arguments is never called. A kit without it behaves exactly as
before. The browser preview models neither the choke nor `finished`.

Kits use the same fixed shared-state and voice-pool rules as synths. Their
code and state are linked once per machine, shared across tracks; voices
still require independent state. Machine code and model data need no file loading when notes or patterns change.

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

### Own screen

From SDK 1.8, a sample engine forking the sampler (`"base": "sampler"`) may
draw its own screen over the sampler's page, as a synth draws its page: it declares
`resources.display` in `definition.json` and supplies `display.cpp` and
`panel.svg.fragment` ([Display API](display.md)), and each build gives the
screen a page as it gives a synth's ([Identity](#identity)). The screen covers the
sampler's page whenever a track using the engine is selected and that page is
showing; the stock sample browser and recorder keep their own pages. The
encoders then edit the engine's controls as on a synth's page:
a press switches between the first four controls and the second four, Shift
turns a continuous control in fine steps, an enum steps one choice per detent,
and edits are saved like a synth's. The engine plays the
sample between its outer markers; inner loop markers do not narrow it.
Display code can draw the sample from its
[overview](display.md#sample-overview).

**What this gives up.** The own screen takes over the sampler page's encoders, so
the stock sampler's own controls are gone from it: a player can't set the sample's
start and end, its loop, its gain or the other things that page's encoders edit, from
there. `SampleBlock.region` still reports what the region holds: the whole sample when
one is newly assigned, or whatever a preset or the same track on the stock sampler
left. An instrument can give itself its own controls for these and read them in
`render_sample`, or hand some of them back to the stock page with `stockShift`.

**Stock controls on Shift (`stockShift`, SDK 1.10).** `resources.stockShift` lists, for
encoders 1 to 4 in order, which of the stock sampler page's controls Shift and that
encoder edit. Stock then edits, steps and saves the setting with the track, as on its
own page, and the engine reads it from `SampleBlock.region`:

| Name | Edits | On the stock page | Read from |
| --- | --- | --- | --- |
| `start` | Sample start | Encoder 1 | `region.begin` |
| `loopStart` | Loop start | Encoder 2 | `region.loop_begin` |
| `loopEnd` | Loop end | Encoder 3 | `region.loop_end` |
| `end` | Sample end | Encoder 4 | `region.end` |
| `direction` | Forwards or backwards | Shift + encoder 1 | `region.reverse` |
| `tune` | Tuning | Shift + encoder 2 | `region.pitch_cents` |
| `gain` | Sample gain | Shift + encoder 4 | `region.level` |
| `window` | The start and end together | Encoders 1 and 4 | `region.begin`, `region.end` |
| `loopWindow` | The loop start and end together | Encoders 2 and 3 | `region.loop_begin`, `region.loop_end` |
| `""` | The machine's own control, in fine steps, as without `stockShift` | | |

A window moves its two markers by the same turn, keeping its length, and stops at
the sample's ends. Each name may appear once; a list shorter than four leaves the
remaining encoders on fine steps. For example, a granular engine might choose:

```json
"stockShift": ["start", "end", "window", "gain"]
```

and a looping one `["loopStart", "loopEnd", "loopWindow", "tune"]`. Only Shift turns
on the engine's own page change; everything else stays as it was:

- Turns without Shift edit the machine's own controls, and a press still switches
  between its first four and second four.
- An encoder the list leaves empty keeps Shift's fine turn of the machine's control.
- The track's envelope, filter and LFO pages are stock's, with or without Shift.

The engine plays begin..end as before. The loop markers are reported for the engine
to use as it chooses; stock's own loop does not apply to it. Stock's loop crossfade
(Shift + encoder 3) and loop mode (Shift + encoder 3 press) aren't offered:
`SampleBlock` doesn't carry them. `stockShift` needs an own screen and SDK 1.10.
While Shift is held, the screen's input `shift` is 1, so it can show what Shift
edits; the [overview](display.md#sample-overview) gives the start, the end and the
gain. The
stock sample browser and recorder are unaffected.

### Services

Each service is an optional entry in `definition.json` `resources`; omitted
means 0. Callbacks are functions in the machine's namespace.

| Resource | Value | Effect | Callbacks |
| --- | --- | --- | --- |
| `stereoOutput` | 0 or 1 | Output is 64 interleaved left/right frames | |
| `chokeSameKey` | 0 or 1 | A note silences earlier voices on the same track and key (drum sampler base: the same pad, at any octave) | |
| `padControls` | Up to two `{label, bits, signed}` fields, 1..7 bits each (2..8 signed), 9 in all; needs `padMix` | Each key's own small settings on Shift's second row ([drum kits](#synthesis-drum-kits)) | `drum_pad`, `pad_control_text` |
| `padMix` | 0 or 1; drum kits, SDK 1.10 | Shift: the model, choke group and each key's stock pan and level; on the second row the key's pad controls and its own FX I and FX II sends; a held step locks pan, level, pad controls and sends per hit ([drum kits](#synthesis-drum-kits)) | |
| `padSettings` | 0 or 1; drum sampler base | The SDK saves 18 machine-defined bits with each pad | `sample_defaults`, `sample_assign` |
| `drumControls` | 0 or 1; needs `padSettings` | The drum page's machine encoders and panel | `drum_control`, `drum_panel` |
| `sliceTiming` | 0 or 1; needs `drumControls` and SDK 1.7 | Lazy-chop audition through the machine, with a matching source-frame tap clock | `slice_clock` |
| `stockShift` | Up to four names, one per encoder; needs an [own screen](#own-screen) and SDK 1.10 | Shift and those encoders edit the stock sampler page's controls ([own screen](#own-screen)) | |
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
| `region.loop_begin`, `region.loop_end` | SDK 1.10: the pad's loop markers, within the sample. No loop when `loop_begin` is at or past `end`; both are `end` where nothing sets a loop, as in a lazy-chop audition |
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
| `sdk` | The SDK version the machine targets, `"<major>.<minor>"`. This SDK is 1.10 and builds 1.0 to 1.10 machines |
| `kind` | `synth`, `drum`, `sample`, `effect` or `player` |
| `description` | Plain text of at most 160 characters, shown by the builder and preview |
| `base` | Sample engines only, and required for them: the stock sampler it forks, `"sampler"` or `"drumSampler"` ([sample engines](#sample-engines)) |
| `sources`, `includes` | Optional extra translation units and include folders inside the machine folder; synths only, since the native plan compiles one source for the other kinds |
| `icon` | Optional builder glyph: SVG path data in a 46 × 48 box, drawn as a stroke. The kind's glyph is used otherwise |
| `presetDefaults` | Optional `{"amp": {...}}` overriding generated factory preset envelope values: `attack`, `decay`, `release` and `sustain`, each 0..32767. It does not rewrite existing projects or presets |
| `presets` | Optional folder inside the machine folder holding the machine's own [presets](#presets); `sdk` 1.3, instruments only |
| `samples` | Optional folder inside the machine folder holding a sample engine's own [samples](#own-samples), which its presets load; `sdk` 1.10, sample engines forking the sampler |
| `graphics` | Optional `{"grayscale": "panel.grayscale.svg.fragment"}` for a synth's developer-authored [grayscale artwork](display.md#grayscale-artwork); `sdk` 1.5 |

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
  preset never points into a user's sample library. A sample engine shipping
  its [own samples](#own-samples) may point at those instead. Other fields stay
  as the device saved them.

#### Own samples

From SDK 1.10 a sample engine forking the sampler (`"base": "sampler"`) can ship
samples of its own, so its init preset and its other presets open with a sound
loaded instead of an empty track. `machine.json` `samples` names a folder of
`<name>.wav` files, and `sdk.py build` installs each as
`content/samples/<id>/<name>.wav`. They live in the OP-XY's content storage, not
in the module memory: stock loads one into its sample memory when a preset
pointing at it is loaded, as it loads any other sample, and the player can still
load another sample over it.

Ship only audio you made yourself or may redistribute under the machine's
licence: never the OP-XY's own content, and no commercial, library or other
third-party sounds you can't relicense. Say in a `README.md` in the samples
folder where each sample comes from, how it was made and under which licence it
ships. Catalog review checks it.

- A name is lowercase letters, digits, `-` and `_`, at most 24. Other files in
  the folder, except `.md` and hidden files, are refused.
- At most 4 samples, each at most 2 MiB: a WAV file of plain 16-bit PCM
  (format 1, not `WAVE_FORMAT_EXTENSIBLE`) at 44.1 kHz, mono or stereo.
- Every sample is loaded by at least one of the machine's [presets](#presets), so
  `samples` needs `presets`.
- A preset points at one in its single sampler region, as stock saves a sample
  engine's preset. Its `sample` is the installed path,
  `content/samples/<id>/<name>.wav`; its `framecount` is the sample's frame
  count; `sample.start` (0 if absent) and `sample.end` lie inside it, and the
  loop markers inside them. `pitch.keycenter` and `hikey` are 0..127, `gain`
  is -30..20 dB, and `reverse` and the loop flags are true or false. `tune`,
  `pan`, `fade.in`, `fade.out` and `loop.crossfade` stay 0: the SDK doesn't
  record their saved ranges yet.
- Presets name the file, so it is installed once however many presets use it.
  A machine without `samples` keeps empty `regions`.
- Projects and presets name a sample by its path. Treat a shipped sample as
  fixed: replacing or removing it under the same name changes or silences every
  project and preset that loads it, and builds never delete files an earlier
  build installed. Give new or changed audio a new name.
- The files share `content/samples/` with whatever else is on the OP-XY. The SDK
  doesn't record the names of stock's own sample folders, so it can't refuse a
  machine ID that matches one: check the device's `/content/samples/` before
  choosing a sample engine's ID.
- The browser builder doesn't install samples yet. Its kit still offers the
  machine and its presets, but not the samples: there each preset opens with no
  sample loaded, as a sample engine's presets did before. `sdk.py build`
  installs the samples and the presets as they are.

```json
"regions": [{"sample": "content/samples/mysampler/pad.wav", "framecount": 264600,
  "sample.start": 0, "sample.end": 264600, "pitch.keycenter": 48, "hikey": 127,
  "gain": 0, "tune": 0, "reverse": false, "loop.start": 0, "loop.end": 264600,
  "loop.crossfade": 0, "loop.onrelease": false}]
```

### Identity

A machine is identified by its `definition.json` `id`. Presets save that name,
so it never changes once published. `machine.json` holds no numbers: each
build gives every machine the stock numbers projects save, an engine number
(an effect number for an effect) and a screen number, and keeps them.

| Kind | Engine or effect number | Screen |
| --- | --- | --- |
| `synth` | Engine 34..255 | Its own, 82..255 |
| `sample` | Engine 34..255 | Its base's: the stock sampler's or drum sampler's. An [own screen](#own-screen): its own, 82..255 |
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
A sample engine's own screen takes a page as a synth's does, and one that
gains a screen keeps its engine number and is given a page.
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

## Note players

SDK 1.8 adds `"kind": "player"`. A player transforms live and sequenced notes
before the track's instrument receives them. Its `machine.json` declares no
identity or base; the layout assigns an unused player type and its own page.
Use four parameters, `"resources": {}`, `player.cpp` and `panel.svg.fragment`.
Players have no instrument presets, audio-render CPU estimate or browser audio preview.

Include `<opxy/player.hpp>` and implement, in the machine's namespace:

```cpp
struct State { /* zero-initialized, at most 2048 bytes, alignment at most 4 */ };
bool press(State&, const opxy::player::Controls&, opxy::u32 note,
           opxy::u32 velocity, opxy::player::Plan&);
bool release(State&, opxy::u32 note);
void display(const State&, const opxy::player::Controls&, opxy::player::Display&);
```

`press` returns false to pass a note through, or true with a plan of zero to
eight tones. Each tone has a MIDI pitch and velocity, and a delay in stock
player steps. Zero sounds on the next step. A plan's tones release together
with its root; at most eight roots can be held. A ninth plan is dropped.
`release` returns true for a silent control key, false to let stock release
its chord. Note callbacks do not allocate, block, log, perform I/O or draw.
State holds no resources. Display reads are advisory while sequencing runs.

For live changes to held chords, optionally implement
`bool update(State&, const Controls&, Update&)`. After a taken press or any
release, and before every stock player step, the SDK drains at most eight
updates, stopping when it returns false. Calls run on the sequencer role,
so held chords can follow control changes without GUI-side note operations.
An update names an existing `root`, a `plan`, and mode `Replace` or `Add`.
`Replace` releases the old tones and schedules the new plan; an empty plan
silences that root. `Add` keeps sounding and queued tones and appends only
new pitches, up to eight total. A change never starts a root with no current
record. Invalid MIDI values or counts discard the update without changing
the chord. Cleared records are immediately reusable, including at capacity.

Optional `void reset(State&)` clears held inputs after stock reset or
all-notes-off; it can preserve machine settings. Without it, the SDK zeroes
State. Keep machine root tracking bounded to eight and clear it on root
release and reset. These callbacks share the same restrictions as `press`.

`Controls.value` contains the four Q31 parameter words. Declared enums with
2..255 choices move one choice per encoder detent, storing the choice's centre.
Other mappings retain the Maestro controller's editing: 64 steps, four choices,
continuous, and off/on, respectively.
`lowest_key` is the track keyboard's lowest F at its current octave. Stock's
octave setter publishes this independently of sample-engine selection.

The page keeps the stock header and uses the four definition labels.
`Display.values` supplies their text; `lit` shows the 24 `<id>_dot_0..23`
circles or filled paths; `text` replaces the centered `<id>_text` text leaf.
All strings must terminate within their fixed arrays. The SDK then fills
`played[4]` from emitted, still-held stock chord records, ignoring queued
strum tones, and supplies `lowest_key` for the output keyboard.

The output keyboard is optional: omit all its key and activity nodes for an
input-only page. When present, it binds `<id>_key_0..127` paths and
`<id>_played_0..127` circles or filled paths. Author white keys as 10 by 44
paths and black keys as 6 by 27 paths at the origin, white keys before black
keys in SVG order, then activity dots. Use explicit fill and stroke and a
`matrix(1 0 0 1 x y)` transform on every binding. The SDK lays out these
keys in the lower 444-pixel-wide row at y=174, fitting whole F-to-E octaves
around the input keyboard and every emitted note. This preserves octaves
under voicing, spread and simultaneous chords. MIDI endpoints use partial
octaves. Hidden nodes move beyond the display.

Player types remain stable when a subsequent build uses the saved layout.
Stock firmware replaces an unknown saved player with Arpeggio. Project
companions currently describe engine/effect identities only: preserve the
firmware's `layout.json` for player identities.
