# OP-XY 1.2.0 firmware contract

How the SDK integrates machines into stock OP-XY OS 1.2.0. This is a
reverse-engineered, version-bound contract for SDK maintainers, not a
manufacturer ABI; machine authors need only the [engine API](api.md).

[The firmware profile](../profiles/opxy-1.2.0.json) is the single source
both the Python SDK and the browser builder read: stock and tool hashes, the
reservation and its table bytes, identity ranges, stock catalog insertion
offsets and every edit. Heap, table and text edits carry their replacement bytes; a
hook names the module symbol it calls or jumps to, and each build computes and
verifies its bytes. The builder pins the profile digest. Stock
addresses live beside the code that uses them: the adapter templates in
`sdk/platform/opxy-1.2.0/`,
[`stock_tables.py`](../tooling/stock_tables.py) and
[`firmware.py`](../tooling/firmware.py). This page says what they mean.

## What a build changes

The input is decrypted stock `os.ldr` 1.2.0
(`e9dbdaa1…98f2`, pinned in the profile). An encrypted `.tfw` or any other
version is rejected. The output differs from stock in exactly these places:

| Change | Edits | Purpose |
| --- | --- | --- |
| Reservation | 2 | Shrinks the application heap by 2 MiB (20 MiB to 18 MiB); the module lives in the freed tail |
| Hooks | 5 | Branches from the synth initializer, both screen factories and both catalog-insertion calls into SDK startup code |
| Sample dispatch | 2 | Recognizes owned SDK sample registrations and selects the stock multisampler audio payload |
| Sample analysis | 10 | Schedules bounded background work, snapshots committed assignments, releases cached analysis and reports progress |
| Drum-page services | 14 | Routes controls, octave publication, assignment and waveform projection for machines declaring pad settings or drum controls |
| Synthesis drum transport | 10 | Classifies owned drum kits for per-key editing, lookup and preset persistence while retaining their registered engine IDs |
| Recorder | 6 | Recognizes owned sample engines when the stock recorder selects, releases and saves a pad, and assigns completed recordings to owned pads |
| Relocated tables | 22 | Moves the effect records (63), engine records (63) and screen slots (128) into the last 8 KiB of the reservation |
| Screen order | 1 | Paints a selected owned synth page before stock overlays |
| Synth and effect controls | 5 | Lets an encoder press on an owned synth's page select an alternate parameter, which its turns and titles then follow; scales fine turns on owned synth and send-effect pages; names an owned effect's division values on the send page |
| LFO destinations | 15 | Selects and modulates all declared engine parameters on owned instrument machines with five to eight parameters, using their assigned encoder icons |
| Display note releases | 2 | Observes both stock note-off map updates while preserving their original calls |
| Project companions | 4 | Writes a [companion](projects.md) after every project save, and shows companions beside projects over MTP |
| Per-voice profiling | 10 optional | Bypasses diagnostic timing in both stock voice renderers when built with profiling off |
| Startup label | 1 | Shows `OPEN <version>-1.2.0` at boot, `<version>` from [`VERSION`](../VERSION), through the stock startup text renderer |
| Module | 1 record | One loader block holding the linked module |

The sample rows (sample dispatch, analysis, drum-page services and recorder)
form the profile's `samples` service: a build applies those edits, and links
the SDK code behind them, only when it includes a sample engine or synthesis drum kit. Synthesis drum
transport is the `drums` service, used only by drum kits. Otherwise their sites
stay stock. Everything else must reconstruct byte for byte. Protected ranges in
[`pipeline.py`](../tooling/pipeline.py) admit only hooks into the module and
the reviewed startup label.
The bootloader is never modified.

The `profiling: false` profile hooks apply only when the build disables
per-voice profiling. SDK builds default to on; the website selects off
([build flag](build.md#4-build)). Six hooks cover the normal renderer's
oscillator, filter and whole-voice measurements, and four cover the alternate
renderer's oscillator and whole-voice measurements. The shims in
[`profiling.s`](../sdk/platform/opxy-1.2.0/profiling.s) replay useful loads mixed
into stock bookkeeping and jump to stock continuations in the existing frame.
They preserve audio processing and voice-lifetime return paths. Counter
initialization, resets, exporters, coarse timing and deadline clocks remain
stock. These shims are linked in both modes so the browser can select the mode
without recompilation; enabled builds leave all ten stock hook sites intact.

## Startup

1. The synth initializer's exit registers every effect, then, on the audio
   role, every synth and sample engine.
2. On the GUI role, a synth registers only after its screen is ready: the
   screen factory exit constructs it and checks its loaded artwork.
3. The catalog adapter admits a machine's menu entry only while its engine
   record and screen are owned and ready, and delegates every other name to stock.

Registration is checked: the record must be empty or already ours, and the
name map must not point elsewhere. The stock initializer clears and refills the
records on every run, so registration is re-checked each time rather than cached.

The startup and catalog glue is C++ generated per collection
([`startup.cpp.in`](../sdk/platform/opxy-1.2.0/startup.cpp.in)); the checks are
[`registration.cpp`](../sdk/platform/opxy-1.2.0/registration.cpp), bound to the
stock tables in `registration_stock.cpp.in`. The two initializer hooks jump
into the middle of stock functions, so
[`startup_exit.s`](../sdk/platform/opxy-1.2.0/startup_exit.s) calls the C++
and then completes the stock epilogue it replaced.

## Identities

| Table | Size | Never use | Notes |
| --- | --- | --- | --- |
| Engines | 63 (relocated) | 33 (stock "none"), IDs stock registers | IDs above 32 exist only with the relocated tables |
| Screen pages | 128 (relocated) | 81 (stock "none") | Sample engines show stock page 2 or 7 instead |
| Effects | 63 (relocated) | 19 (stock "none"), IDs stock registers, stock engine IDs (2–4, 6, 7, 20, 22, 29–33) | Free IDs: 1, 8, 11, 21, 23–28, 34–62; all effects use page 21 |

Presets store the engine **name**. Projects store the engine **number**.
Firmware without the relocated tables opens a project that uses an engine above
32 with Prism (18) instead. A firmware's numbers therefore never change: layouts
keep them ([identity](api.md#identity)), and engine and page IDs reach the
module only as absolute link-time symbols, so one set of objects serves any
numbering.

## Synth voices

Stock asks the factory for a voice object per voice index (0–255) and calls
it through an 11-entry method table: render once per 64-sample block, start on
a note, and unchanged stock defaults for the rest (see
[`voice.cpp.in`](../sdk/platform/opxy-1.2.0/voice.cpp.in)). It never checks
for a missing object, so the factory gives every index a small handle. A
machine's `voiceCapacity` voices go to handles as their notes start: a free
voice, or the one whose note started longest ago. A handle that has lost its
voice renders silence. One adapter serves every synth.
Render receives 64 output words and 8 parameter words. The SDK adds the stock
per-sample pitch increments. Output is mono Q31 peaking at most at half scale. Stock
velocity, envelope, filter and mixing run afterwards.

## Screens

A synth's page is a stock screen object: 76 stock bytes, plus adapter state,
plus an SVG asset named after the machine. The SDK screen shell
([`screen.hpp`](../sdk/platform/opxy-1.2.0/screen.hpp)) supplies the four
control titles and values that stock fills in. The machine's artwork
supplies the rest. One drawing renderer
([`screen_drawing.hpp`](../sdk/platform/opxy-1.2.0/screen_drawing.hpp)),
sized by the machine's declaration (see the [display API](display.md)), binds
the artwork's nodes, text slots and palette entries by ID and applies each
frame through stock's own setters: matrix words for a node's transform, palette
selectors for its paint, path commands rewritten in place for outlines, and
text leaves. Nothing is created or freed.

Drawing input reads the GUI's existing note-start counter at `8d4d75c0` and
the per-track 128-pitch note map at `86be3f14` (the reader at `8d5b5e88`, set
at `8d5b5ecc`, clear at `8d5b5ea4`). The calls to clear at `8d5937ea` and
`8d59384c` route through `sdk_note_off`: it checks whether that pitch was held,
delegates the unchanged clear exactly once, preserves its result, and counts
the closure. The screen reads the held count and release serial on the GUI
side; no audio callback or cross-role publication is added. The two calls are
authenticated profile edits. Other stock instructions and note routing remain
unchanged. This reports stock note activity, not audio-envelope state.

Stock paints the screen slots in ascending page order each frame, so page
numbers are layers. The compositor's loop advance is hooked: when the selected
page is an owned synth's, it is painted right after slot 0, beneath stock
overlays such as the octave indicator. Every other slot keeps its order and
page IDs are unchanged.

The stock SVG parser **asserts (halts) on a number with five or more decimal
places**, and parses exponents on a separate, unverified path. Every authored
asset is limited to at most four decimal places and no exponents.

### Alternate controls

On the engine page of an instrument track, an unshifted turn of encoder `e`
edits parameter `e` through stock's controller, which owns stepping, parameter
locks and publication; shifted turns edit `4 + e`. The engine page's
parameters are the first eight words of the vector the voice renders. Stock's
key dispatcher passes an encoder click (released without a turn or shift) to a
handler the engine page ignores.
[`synth_controls.cpp`](../sdk/platform/opxy-1.2.0/synth_controls.cpp) replaces
both calls: while the engine page shows an owned synth with
[alternates](api.md#alternate-controls), a press moves the position to its next
parameter slot and a turn edits that slot. For owned synths, Shift's indices
4..7 map back to the same shown positions; continuous controls receive a copy
of the turn event with its step divided by ten, keeping detents and
acceleration. Stock consumes the event synchronously and still owns lock
editing and publication.
Stock steps a control by its engine's choice count, which its table
(`0x8c0b2fc8`) gives only stock engines, and otherwise by the event's amount.
Both of its step calls already reach `sdk_lfo_step`; for an owned synth's enum
it returns the next choice's midpoint (`sdk_synth_step`, from the definition's
label count), one choice a detent and two when the turn is accelerated. The selection is GUI state per track, reset when the
track's engine changes. Other pages and unowned engines retain stock routing.

The auxiliary-track controller already folds Shift into positions 0..3; owned
send effects on tracks 14/15 get the same fine scaling. Encoder turns clear the
pending-click flag, so a press-and-turn does not also click on release.

Stock draws each position's title from the engine record's parameter-name
callback and its value as a percentage of the vector word. The adapter
passes the selected track to the screen's parameter-vector getter after
refreshing alternate-control selection.
The engine-page title call maps the position to its shown slot; the record's
parameter-name callback always names the raw slot, including when called by
an LFO page. The screen shell passes stock a copy
of the vector with each position's shown word in place. Generated metadata
then supplies one-decimal amounts (0.0–100.0 without a suffix) and bounded positive
anchor-ratio captions on custom synth screens; enums and machine text overrides remain
discrete. FX pages retain their stock captions, except that a send page
hook (`8d53aa6e`, the value-text call in its loop over positions) lets an owned
effect name a `division` control's value, as stock does only for its delay
(effect 0, table `0x840535fc`).

### LFO destinations

[`lfo.cpp`](../sdk/platform/opxy-1.2.0/lfo.cpp) extends the engine destination
group of the Value, Random and Element LFOs on instrument tracks 0..7.
Generated metadata supplies each instrument's declared parameter count, encoder assignments and
record-ownership probe, without requiring its screen to be loaded. Only an
owned machine with five to eight parameters uses the extended routing.

Four calls to the stock range helper (`8d53ec60`, `8d53ef50`, `8d53ff32`,
`8d5426b8`) retain the selector's eight positions instead of folding them
into four. The live and parameter-lock quantizer calls (`8d5ab8ce`,
`8d5ab7ea`) step among the declared slots and store their Q31 midpoints.
The instrument controller's click-publication call (`8c0b38b4`) cycles the
current full slot modulo the declared count before delegating to stock's
original setter. This bypasses its four-position click quantization without
changing other parameters or destination groups.
The engine-group audio call (`11a03f20`) takes the full selector from R4,
decodes the same slot and delegates to stock's modulation function, which
still owns saturation and restoration. This path reads the audio engine
table and record ownership only; it does not call GUI or project getters.
It also corrects stock's destination scratch word (`84044a58`) before the
existing modulation telemetry publication (`11a042e8`). The GUI therefore
animates the same full parameter slot that audio modulates, while stock
retains the publication timing, amount and selected-track filtering.
Other destination groups and stock engines retain their original routing.
Three icon-strip animation calls (`8d53f676`, `8d540b6e`, `8d5431ca`)
map slots 4..7 to their declared encoder using the caller's decoded group
and slot; sample engines repeat the four positions for their second row.
Element's name call (`8d53f3e8`) also restores the strip's
visibility for those slots. Its initializer's offset calculation (`8d53ecc0`)
uses the same mapping after stock stores the full slot, so reopening the page also shows the
correct icon. The full slot still selects the label and audio
destination; only the strip's scroll position is remapped.
The three name calls (`8d53f3e8`, `8d5404b6`, `8d542da8`) and icon mapping
clamp retained selectors beyond the declared count to the last slot, just
as audio does. Changing from a larger machine therefore keeps a valid name
and icon until the user edits the destination.
The [API guide](api.md#alternate-controls) describes saved-selector compatibility.

## Sample engines

Stock's sampler, multisampler, drum and drumline engines are synth
registrations with record flag 1. Before calling such an engine's factory, the
voice allocator acquires the track's cached sample and publishes it in a global
that the factory reads. The SDK registers sample engines the same way and
passes the sample as a read-only `opxy::SampleView`: 16-bit PCM in RAM, frame
count, rate and channel layout. Tempo, tick position and play state come as an
advisory `opxy::Transport`. Sample selection uses the stock sample page.

Sample engines declaring stereo output write 64 interleaved L/R frames to
the stock 128-word scratch buffer and select the stock stereo mixer; mono
declarations retain 64-word output.
The stock stereo PCM reader consumes packed left/right 16-bit pairs, while
its output writes alternating 32-bit left/right words.

Requirements: flag 1, an engine ID of 34–62, and page 2 or 7. The engine never
touches the sample cache or its lock.

Stock's sample predicate and note-region dispatcher also have explicit engine
ID checks. One generated table describes the collection's sample machines
([`sample_callbacks.cpp.in`](../sdk/platform/opxy-1.2.0/sample_callbacks.cpp.in));
[`sample_machines.cpp`](../sdk/platform/opxy-1.2.0/sample_machines.cpp)
admits an ID only while its registration is still ours, and an owned engine
borrows the multisampler payload. Track and voice IDs stay unchanged. Stock,
absent and conflicting registrations follow the original paths. The first
sample machine in a build enables the `samples` service; further ones add no
firmware patch sites.

Stock starts these voices through a separate note-start method; factory
creation alone does not start or choke a voice. Page 7 maps notes to
the stock 24 canonical pads. Optional same-key choke is scoped to track and
canonical pad, and checked before DSP or analysis lookup.

### Hooks

Hooks that replace a CALL are C++ with the C ABI. Hooks entered inside a stock
function are tiny assembly stubs: they save everything compiled C++ may
clobber through one routine pair ([`hook.s`](../sdk/platform/opxy-1.2.0/hook.s):
R0-R3, P0-P2, ASTAT, the I/M/B/L registers, both hardware loops and both
accumulators), call C++, and replay or deliver what stock expects. Stubs remain
only where the contract is not C: an argument in R5, outputs in R5/R7/P4/CC, a
return-address marker, the caller's frame, and replayed instructions.
Stock addresses used by more than one unit are in
[`stock.hpp`](../sdk/platform/opxy-1.2.0/stock.hpp); every sample record
offset is in [`stock_sample.hpp`](../sdk/platform/opxy-1.2.0/stock_sample.hpp).

### Sample analysis

The analysis service owns preparation, allocation and reclamation; the audio
callback only reads published data. Entries are keyed by sample object and
engine. The GUI marks assigned paths of analysis machines through the cache
message's priority byte, which carries only "assigned", so the file worker
prepares each such sample for every registered analysis machine, each within
its own budget. Browser page 4 suppresses new requests: stock auditions
replace regions while it is open. Results are released before stock frees the
PCM object.

### Pad settings and the drum page

Pad settings occupy the stock pad record's fade word as a tagged 16-bit value,
padded to survive stock's integer/float serialization. For a machine with pad
settings the drum bridge owns that word: stock does not paint a fade, the
Shift encoders 2/3 never reach stock, and presses never enter stock's mode
cycle. With drum controls they reach the machine instead. Unowned
registrations delegate to stock.

The assignment bridge validates worker generation, track, key and frame count
before supplying file metadata to `sample_assign`, then initializes the owned
region over the full loaded sample and restores the replaced pad's pitch,
reverse and volume before stock publishes it. A completed recording takes the
same path when the recorder initializes its region, with `recorded` set, the
recorder's frame count and 44.1 kHz rate, and no file metadata; it never
consumes the file worker's pending metadata. The stock loader's 882,000
decoded-frame cap still applies. Assignment looks up the raw note once and
stores the canonical key through the owned initializer hook, avoiding a
second octave conversion.

The drum page paints a temporary copy of the pad record: the machine's
effective pitch and play mode, never the saved tuning, markers or reverse. A
local GUI parameter vector keeps stock from adding the live pitch offset twice;
the original vector still drives stock's waveform trim overlays. Owned pages
show the outer markers. The live/step editor's pitch classification also
recognizes owned drum-page machines, so parameter zero uses stock's ten-cent
stepping and lock handling. Saved pad pitch uses the same step, stored in
stock root/fine fields and published through the existing region handler.

The sample adapter reads track transpose from the stock per-track audio
parameter table using stock note-start's 73-position mapping to -36..36
semitones, separately from pitch increments, which also include absolute note
pitch and modulation. Drum-key routing passes stock's track octave to its key
mapper and publishes the octave in a per-track word; the octave setter also
publishes each change. Audio maps notes with that snapshot and the same
integer mapping, without calling GUI project or pattern getters. Choke uses
the canonical pad; the raw note minus the canonical pad is `drum_transpose`.

## Project companions

Stock saves every project, the workspace included, through one function,
`save(project, path)` (`0x8d5973ac`), which `0x8d584534` calls once, at
`0x8d584576`, with stock's project object. That call is hooked: the SDK calls
stock `save`, and when it succeeds reads the saved file back with stock's
newlib `fopen`, `fread` and `fclose` (`0x8fed0350`, `0x8fed05c8`,
`0x8fed00b6`) for its size and CRC-32, then writes the companion beside it with
`fwrite` (`0x8fed078c`) ([`project_companion.cpp`](../sdk/platform/opxy-1.2.0/project_companion.cpp)).
The machines it lists are the collection's, from their link-time numbers; a
kit leaves out machines with number 0. A failed save writes nothing.

Stock's MTP view lists only paths matching a table of regular expressions
(`0x840aade4`…), and under `/projects` only `.xy`. Three text edits rewrite
the workspace and project patterns in place to `\.o?xy$`. Dropping a redundant
`\d` (already in `\w`) from one character class keeps each string's length;
the workspace pattern uses its padding.

## Send effects

Effects live in a separate registry (19 stock entries, 63 relocated). Stock
registers send effects and the per-track filter types in it; the filter setter
keeps the stock bound, so IDs above 18 are send effects only. Stock firmware's
send-slot setter (`0x8d50e0c4`) rejects types above 18. The audio mixer keeps one
**480-byte** slot per track (`0x84c6e3b8 + 480 * slot`); the two sends are
slots 14 and 15, and either may hold any effect, including the same one. When a
send's effect changes, the old object fades out over a few blocks and only
then is the new one constructed in place, so a slot never holds two live
effects. The mixer calls `init(object, slot)` after construction and again on
an engine reset, then `process(left, right, parameters)` once per 64-sample
block on in-place stereo buffers. **No destructor ever runs**, so an effect may
hold no resources. The first slot word is the object's method table, which
leaves 476 bytes of effect state.

A send track reports its effect ID where an instrument track reports its
engine, and stock's parameter stepping (`0x8d5ab584`) treats that ID as an
engine. On the first four controls, ID 2 gets engine 2's eighth-size steps,
which round a detent to nothing, and IDs 3 and 4 get pitch stepping in cents.
Other stock paths test engines 6 and 29–31, and split at 29. Effects
therefore use only free IDs that no stock engine holds and stock never tests:
1, 8, 11, 21, 23–28 and 34–62, never the stock "none" engine 33. For the same
reason a layout never gives an effect and an engine the same number, so an SDK
hook that asks about an engine number never mistakes a send track for an SDK
engine.

Stock delay, reverb and chorus keep their delay lines in a static per-send
scratchpad, two 1 MiB regions that `scratch(slot, bytes)` (`0x8d50d9c8`)
returns for slots 14 and 15, at most 512 KiB per request; it is never cleared
or freed. An effect that declares `bufferBytes` gets the same memory: the
adapter's `init` asks `scratch` for its slot, only for slots 14 and 15, and
keeps the address in the word after the effect's state, which leaves at most
472 bytes of state.

Each effect also answers a dry-level query, `(object, parameters) -> Q31`;
stock delay and reverb return parameter word 3. The stock mixer reduces the
source dry contribution by `send * (1 - dry)`. The effect's DSP
supplies only wet audio. The generated adapter exposes the machine's declared
`dryParameter`, or returns `INT32_MAX` when none is declared, matching stock's
absent-object fallback. This is a source-mixer control, not a wet/dry crossfade
inside the effect. See the [effect API](api.md#effects).

## Synthesis drum transport

An owned `drum` registration uses flag 1, its own engine ID and drawing page.
The SDK aliases only the local Drumline classification branches to engine 4;
stock and occupied registrations retain their behavior. Twelve exact hooks cover
the drum predicate, region lookup, note start, project load, editor dispatch and
preset import/export. Project load's track check at `8d587560` resets a
Drumline track's eight parameters to neutral offsets (`0x40000000`); without
the alias a loaded kit kept zero offsets and every note decoded 64 below its
stored values. Note start tests for Drumline at `8d593d84`; only that branch
finds the played pad's region and packs its index into bits 16..23 of the note
word, which `8d519c30` stores in voice `+0x5c` and the constructor turns into
the region pointer at `+0x10`. Without the alias every note carries index 0.
The shared sample note hook selects the Drumline payload only for owned kits.
Drum classification checks exact engine-record ownership without requiring a
GUI screen: the audio role registers engines but does not construct their pages.
Catalog and screen readiness retain their loaded-page checks.
The classifier result is stored in voice byte `+0x4e` at `8d51a258`.
The renderer chooser at `8d51afba` selects the drum renderer `8d51a684`
when that byte is nonzero. Its call at `8d51a96a` uses vtable `+12` with
`(self, output, region, track_parameters)`; the fourth argument is on the
stack. The payload is passed directly, rather than through the normal
renderer's published region pointer. The constructor calls vtable `+20`
at `8d51a358` with `(self, key, velocity, region, track_parameters)` to start
the note. Both calls have dedicated SDK adapters. Vtable `+24` reports whether
the handle still owns a voice. Zero causes the renderer to report completion;
the caller at `8d51bbda` retires it after mixing that block. Active handles
remain allocated until the stock amplitude envelope completes or the SDK
steals their slot. Vtable `+32` returns zero
for mono output; the unused normal-render entry `+8` fills its output with
silence. The kit never calls stock Drumline synthesis.
Unlike the normal synth path, this renderer leaves amplitude application to
the drum DSP. The SDK multiplies normalized output by each Q31 envelope/gain
word at `11905300` exactly once, using the native fixed-point multiply. It
retains the SDK's normal gain query; Drumline's private output-level shifts
do not apply to the public Q31 contract.
The drum renderer writes the pitch buffer at `11904b00` with each phase
increment shifted right by 16 (`8d51a850`–`8d51a854`). The SDK expands those
64 values back to the public `Pitch` units before the contributor callback;
the lower 16 bits are zero because stock has already quantized them.

Each key's four parameter words store 60 bits that stock exports as positive
Q15 integers and restores with a left shift of 16. The SDK packs the key's
eight 7-bit controls into 56 bits and its model into the other 4. The pad's
model byte (record `+0x18`: Drumline's instrument type in the low nibble, its
choke group in the high one) marks the layout with type 7, which stock's
editor skips (`8d4d69d0`) and its default kit never uses; the choke group is
kept unless Shift and the second encoder choose one. The high nibble is the
pad's play mode, which presets name in a region's `playmode`: 0 `gate`,
1 `oneshot` (the default, omitted), 2 `mute1`, 3 `loop`, 4 `mute2`,
5 `mute3`. Pads sharing a mute cut each other off. Stock's default kit has BD
and SD in `mute2`, CH, OH and RC in `mute1`, the rest `oneshot`. A preset
names type 7 `fm slap`, and its `params` are the top halves of the four
words. On the audio side the note payload's first word is stock's reading of
that byte, `(6 type + group)` over 66 bins (`8d5aba1c`), so type 7 is bins
42..47. Every other saved pad field is audible (`pan`, `gain`, `tune`,
`group`) or not exported in presets (`+0x19`), so the layout holds at most 16
models. Unmarked pads, including the earlier tag-10 eight-value layout, use
the key's defaults. Stock's per-key editor (`8d4d6908`, jump table at
`8403619c`) gives controls 0..3 the four words and Shift's encoders 4..7 the
type, `+0x14`, pan (`+0x1e`) and gain (`+0x1d`); the SDK gives Shift's first
encoder the model and the others their shown control. Track modulation is
applied after decoding. GUI edits repack and use the stock dirty/publication
calls; no audio callback writes preset state or performs file I/O.
