# OP-XY 1.2.1 firmware contract

How the SDK integrates machines into stock OP-XY OS 1.2.1. This is a
reverse-engineered, version-bound contract for SDK maintainers, not a
manufacturer ABI; machine authors need only the [engine API](api.md).

[The firmware profile](../profiles/opxy-1.2.1.json) is the single source
both the Python SDK and the browser builder read: stock and tool hashes, the
reservation and its table bytes, identity ranges, stock catalog insertion
offsets and every edit. Heap, table and text edits carry their replacement bytes; a
hook names the module symbol it calls or jumps to, and each build computes and
verifies its bytes. The builder pins the profile digest. Stock
addresses live beside the code that uses them: the adapter templates in
`sdk/platform/opxy-1.2.1/`,
[`stock_tables.py`](../tooling/stock_tables.py) and
[`firmware.py`](../tooling/firmware.py). This page says what they mean.

## What a build changes

The input is decrypted stock `os.ldr` 1.2.1
(`60358afd…b52f`, pinned in the profile). An encrypted `.tfw` or any other
version is rejected. The output differs from stock in exactly these places:

| Change | Edits | Purpose |
| --- | --- | --- |
| Reservation | 2 | Shrinks the application heap by 2 MiB (20 MiB to 18 MiB); the module lives in the freed tail |
| Hooks | 5 | Branches from the synth initializer, both screen factories and both catalog-insertion calls into SDK startup code |
| Sample dispatch | 2 | Recognizes owned SDK sample registrations and selects the stock sampler's (page 2) or multisampler's (page 7) audio payload |
| Preset serialization | 1 | Includes owned sample engines in sample-file and slice-region export while preserving the saved engine identity |
| Sampler page | 9 | Shows an owned page-2 engine's track as the stock sampler's page, with its one sample in region 0 for every key |
| Sample analysis | 10 | Schedules bounded background work, snapshots committed assignments, releases cached analysis and reports progress |
| Drum-page services | 30 | Routes controls, octave publication, assignment, waveform projection, source audition and held-pad slice/copy/paste and multi-edit actions |
| Recorder | 6 | Recognizes owned sample engines when the stock recorder selects, releases and saves a pad, and assigns completed recordings to owned pads |
| Relocated tables | 21 + 1 hook | Moves the effect records (63), engine records (256) and screen slots (256) into the last 16 KiB of the reservation; the engine validity check's 7-bit bound is a call (`sdk_engine_bound`) that compares against 256 |
| Screen order | 1 | Paints a selected owned synth page before stock overlays |
| Synth and effect controls | 7 | Lets an encoder press on an owned synth's or send effect's page select an alternate parameter, which its turns, titles and values then follow; scales fine turns on owned synth and send-effect pages; steps owned enums one choice per detent; names owned enums' choices, and an owned effect's division values on the send page |
| LFO destinations | 15 | Selects and modulates all declared engine parameters on owned instrument machines with five to eight parameters, using their assigned encoder icons |
| Display note releases | 2 | Observes both stock note-off map updates while preserving their original calls |
| Project companions | 5 | Writes a [companion](projects.md) after every project save, maps a project's machine numbers through its companion on every load, and shows companions beside projects over MTP |
| Diagnostic benchmark | 1 | Extends explicit command 0xc8 with the selected machines; no normal-playback callbacks added |
| Per-voice profiling | 10 optional | Bypasses diagnostic timing in both stock voice renderers when built with profiling off |
| Preset scan | 1 | Hides factory presets whose engine the build does not install ([presets](#presets)) |
| Startup label | 1 | Shows `OPEN <version>-1.2.1` at boot, `<version>` from [`VERSION`](../VERSION), through the stock startup text renderer |
| Extension events | 7 | Observes live input, stock transport and queued controls for extensions, and lets a Record double-tap owner edit notes through stock editing and undo; linked only when an extension is selected |
| Module | 1 record | One loader block holding the linked module |

The sample rows (sample dispatch, analysis, drum-page services and recorder)
form the profile's `samples` service: a build applies those edits, and links
the SDK code behind them, only when it includes a sample engine. Otherwise
their sites stay stock. Everything else must reconstruct byte for byte. Protected ranges in
[`pipeline.py`](../tooling/pipeline.py) admit only hooks into the module and
the reviewed startup label.
The bootloader is never modified.

The `profiling: false` profile hooks apply only when the build disables
per-voice profiling. SDK builds default to on; the website selects off
([build flag](build.md#4-build)). Six hooks cover the normal renderer's
oscillator, filter and whole-voice measurements, and four cover the alternate
renderer's oscillator and whole-voice measurements. The shims in
[`profiling.s`](../sdk/platform/opxy-1.2.1/profiling.s) replay useful loads mixed
into stock bookkeeping and jump to stock continuations in the existing frame.
They preserve audio processing and voice-lifetime return paths. Counter
initialization, resets, exporters, coarse timing and deadline clocks remain
stock. These shims are linked in both modes so the browser can select the mode
without recompilation; enabled builds leave all ten stock hook sites intact.

The diagnostic runner calls stock's nine synth tests, then the selected SDK
machines in build order. Synths reuse stock's benchmark-one routine. Effects and
sample engines use generated wrappers beside their real C++ callbacks, isolated
state/heap storage and a two-instruction CYCLES reader. Only the explicit command
publishes those callback statistics into the existing diagnostic records and
exporter. The heap calls match the bounded allocation path used by sample analysis;
allocation, preparation and cleanup are outside the timed callback. No callback
wrapper is inserted into normal audio processing. See [workloads](profiling.md#what-is-measured).

## Stock browser

The internal GUI adapter in
[`stock_browser.hpp`](../sdk/platform/opxy-1.2.1/stock_browser.hpp) reuses the
stock engine/preset browser for flat selectors. It is specific to authenticated
OS 1.2.1 and is not part of the public machine or WASM drawing API. Linking the
adapter adds no stock patch, shortcut, startup action or audio callback.

Declare `opxy::platform::stock_browser::StockSelector<N>` as a zero-initialized
static aggregate. It owns N rows, copied strings, the catalog and its delegate.
Call `open(title, labels, initial, callbacks)` from the native GUI role:

```cpp
#include "stock_browser.hpp"
namespace browser = opxy::platform::stock_browser;
static browser::StockSelector<3> shape_selector = {};
static opxy::u32 selected_shape = 1;
static void choose_shape(void*, opxy::u32 index) { selected_shape = index; }

bool show_shapes() {
    const char* labels[3] = {"soft", "medium", "hard"};
    return shape_selector.open("shape", labels, selected_shape,
        browser::Callbacks{nullptr, choose_shape, nullptr});
}
```

N must be nonzero. Titles and labels must contain 1–15 printable ASCII
characters; `open` rejects invalid strings, an out-of-range initial index or a
missing confirmation callback. It copies strings, so the label array and
source strings need not outlive the call. The selector and callback context
must remain alive and unmodified until the browser closes.

Only one adapter selector may be active. Opening while it or another stock
browser (GUI screen 9) is active returns false without replacing it. Stock
owns the modal screen, scrolling and controller stack: the left encoder
selects rows, either the left or white click confirms, and M2 cancels. Other
stock browser users retain their existing controls. Unsupported encoder
clicks are consumed. The adapter does not change stock turn routing.

Highlighting changes only a pending index. Confirmation receives that index
once; a subsequent close does not cancel it. Closing without confirmation
calls the optional cancellation callback once. Ownership remains held during
callbacks, so synchronous reopen attempts fail. Defer another selector to a
later GUI event. The adapter only delivers a GUI choice; its client owns
persistence and any parameter transport to audio. Never call this API from
an audio callback or publish audio state by sharing the GUI selector storage.

The stock call contract is in `stock_browser.cpp`: browser getter `8c09dc28`,
setup `8c0a0b90(browser, catalog, initial, delegate, mode=0)`, screen getter
`8d550e70`, plain heading `8d551c48(screen, -1, title)` and preset-bank visibility
`8d5501ec(screen, 0)`. The catalog uses 28-byte inline strings, 36-byte items,
72-byte rows and a 20-byte root on the 32-bit target. The delegate's +0x10
method receives highlights, +0x1c handles clicks (0 left, 3 white), +0x24 observes
close, and +0x30 returns an empty options vector through a hidden result pointer.
These details remain behind the adapter; clients must not recreate the ABI or
install global input hooks to use it.

The shared host fixture exercises two clients, input rejection, both stock
click paths, cancellation, modal ownership and callback reentry. Native build
and release verification check the target ABI and reconstruction. Hardware
observations must be tied to the tested image; host doubles do not establish
device timing or acceptance.

## Extension events

The [extension runtime](extensions.md) shares these hooks among the statically
selected extensions; each hook's `events` field in the profile names the public
events it feeds. `extension_stock.cpp` supplies version-specific recording
context, note editing and stock forwarding. Its `sdk_extensions_*` symbols are
named for the stock calls they replace; they are adapter entry points, not
extension APIs. Capture ([open-xy-core-machines](https://github.com/etcetc000/open-xy-core-machines/tree/main/extensions/capture)) is the first
subscriber: it owns the bounded current-track ghost pattern and the Record gesture.

The seven hooks replace the controller-update call at `0x8c0baaf0`, queued key
dispatch at `0x8c0b6184`, normalized live onset/release at `0x8d594376` and
`0x8d59420a`, accepted-note bookkeeping at `0x8d595844`, recorder tick at
`0x8d59819c`, and Stop cleanup at `0x8d597db8`.
The controller-update call follows the GUI tick call in the same main loop;
they are separate sites. The Stop hook observes a stop/restart pair within one controller update. The key
span also replaces the following queue-address load; its wrapper returns that
address in R0. This
permits an absolute call without changing the stock stack or adding assembly.
Immediate input is restricted by stock to keyboard keys 26–49; Record (50)
uses the queue. The live-note wrappers also cancel interrupted tap gestures.
The onset wrapper establishes a scoped live-input identity; only stock-accepted
notes inside that scope enter history. Direct sequencer calls and step entry do
not. Stock input arguments and normal recording actions are forwarded unchanged.

History uses the same position getter, pickup convention and scaled Q8 duration
deltas as stock recording. Pattern quantization remains stock playback behavior.
Completed notes do not expire with elapsed time. New onsets replace same-pitch
notes strictly inside the stock scale-dependent collision radius; a full ghost
pattern rejects new onsets before pruning, as the stock container does. Tick
updates advance held-note duration, capped at one loop. The first clean Record
press snapshots the take; completing the second tap commits that snapshot.
Interrupted/expired pairs never commit the saved snapshot.
Capture replaces the track's notes. After resetting and taking one stock undo
snapshot, the SDK unlinks every note from the stock note list
([`note_container.hpp`](../sdk/platform/opxy-1.2.1/note_container.hpp)), as
stock's own editor does inline; it first validates the whole used and free
lists and refuses, before the undo snapshot, if a note is still being recorded
(flag bit 1). It then inserts the take through the stock note container with
its scale-dependent collision radius, and publishes the pattern once, without
calling the audible note dispatcher or moving the playhead. Stock undo restores
the previous notes. Parameter locks and step components stay. An empty cache
produces no success popup. History
retains at most 120 notes and 20 simultaneously held notes; it is cleared
after a successful insertion, on track/context changes and on a new playback
start. Stop preserves the partial take. Host adapter tests use stock doubles;
they do not establish device scheduling or timing.

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
([`startup.cpp.in`](../sdk/platform/opxy-1.2.1/startup.cpp.in)); the checks are
[`registration.cpp`](../sdk/platform/opxy-1.2.1/registration.cpp), bound to the
stock tables in `registration_stock.cpp.in`. The two initializer hooks jump
into the middle of stock functions, so
[`startup_exit.s`](../sdk/platform/opxy-1.2.1/startup_exit.s) calls the C++
and then completes the stock epilogue it replaced.

## Identities

| Table | Size | Never use | Notes |
| --- | --- | --- | --- |
| Engines | 256 (relocated) | 33 (stock "none"), IDs stock registers | IDs above 32 exist only with the relocated tables |
| Screen pages | 256 (relocated) | 81 (stock "none") | Sample engines show stock page 2 or 7 instead |
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
[`voice.cpp.in`](../sdk/platform/opxy-1.2.1/voice.cpp.in)). It never checks
for a missing object, so the factory gives every index a small handle. Stock
asks for an index's object again before each note it starts there, renders the
index once per block while its note sounds, and never calls the destructor
entries for a note: a note ends when stock stops rendering its index. Stock's
free-voice allocator (`8d51c994`) hands out its 27 voice slots and limits each
track to 8 notes; at the limit it takes the quietest
voice on the track, fades it over one block and restarts the same index
(`8d51ca94`, `8d51ad04`).

Stock has 27 voice slots, so the adapter keeps at most 27 voices, plus one
spare: `min(voiceCapacity, 27) + 1`. They go to handles as their notes start: a
free voice, else one whose index has missed two blocks of renders (blocks are
counted from the renders themselves), or, once nothing of the machine has
rendered for 60 ticks of CPU cycles (`sdk_voice_ticks`, about 20 ms), any whose
note did not just start. Ended voices are released once per counted block, and
at a note start after such a pause. When a whole chord of `voiceCapacity` notes
changes at once on new indices, the spare covers one note and the rest start
about two blocks late, until their old notes have missed two blocks; a capacity
above the chord size avoids that delay. A factory call for an index
frees the voice it held, so a voice stock takes keeps its place for the
restarted note. The spare covers a note that ended just before another starts.
Only with more notes sounding than that (a capacity below 8, or the machine on
several tracks) does a note take a sounding voice: one no other note waits for,
then one past its first 8 blocks (as stock spares young voices), then the one
rendered least recently, then the one whose note started first. As stock does
with a voice it takes, that note fades linearly to silence over its next
block, and the new note starts in the voice once it has, or once stock has
stopped rendering the old note. A handle that has lost its
voice renders silence. One adapter serves every synth.
Render receives 64 output words and 8 parameter words. The SDK adds the stock
per-sample pitch increments. Output is mono Q31 peaking at most at half scale. Stock
velocity, envelope, filter and mixing run afterwards.

## Screens

A synth's page is a stock screen object: 76 stock bytes, plus adapter state,
plus an SVG asset named after the machine. The SDK screen shell
([`screen.hpp`](../sdk/platform/opxy-1.2.1/screen.hpp)) supplies the four
control titles and values that stock fills in. The machine's artwork
supplies the rest. One drawing renderer
([`screen_drawing.hpp`](../sdk/platform/opxy-1.2.1/screen_drawing.hpp)),
sized by the machine's declaration (see the [display API](display.md)), binds
the artwork's nodes, text slots and palette entries by ID and applies each
frame through stock's own setters: matrix words for a node's transform, palette
selectors for its paint, path commands rewritten in place for outlines, and
text leaves. Nothing is created or freed.

Drawing input reads the GUI's existing note-start counter at `8d4d6c08` and
the per-track 128-pitch note map at `86bda258` (the reader at `8d5b7d10`, set
at `8d5b7d54`, clear at `8d5b7d2c`). The calls to clear at `8d5954b2` and
`8d595514` route through `sdk_note_off`: it checks whether that pitch was held,
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
[`synth_controls.cpp`](../sdk/platform/opxy-1.2.1/synth_controls.cpp) replaces
both calls: while the engine page shows an owned synth with
[alternates](api.md#alternate-controls), a press moves the position to its next
parameter slot and a turn edits that slot. For owned synths, Shift's indices
4..7 map back to the same shown positions; continuous controls receive a copy
of the turn event with its step divided by ten, keeping detents and
acceleration. Stock consumes the event synchronously and still owns lock
editing and publication. The selection is GUI state per track, reset when the
track's engine changes. Other pages and unowned engines retain stock routing.

The auxiliary-track controller already folds Shift into positions 0..3; owned
send effects on tracks 14/15 get the same fine scaling. Encoder turns clear the
pending-click flag, so a press-and-turn does not also click on release.

The same press handler sees clicks on a send track's FX page when the
auxiliary controller (vtable `0803fb6c`) is on top. For an owned effect with
alternates, a click moves the position to its next slot, and the turn hook
passes stock the shown slot, so stock's editor (`8d5ad5bc`) steps and
publishes it as any other. The selection is GUI state per send, reset when the
send's effect changes. The send page titles each position through the effect
record's parameter-name callback (`8d506c90`); its call at `8d53b642` is
hooked to name the shown slot. The value-text hook (`8d53b636`) reads a shown
alternate's current word (`8d587708(track) + 0x3864 + slot × 4`) and captions
it as stock does, 00..99, or with the machine's enum or division text.

When an effect is put on a send, stock fills all eight current words with the
effect's defaults but stores and publishes only the first four, so the audio
side would render words 4..7 as 0 until each is edited. Once per frame, from
the compositor's advance at its first page, the SDK checks each send: for an
owned effect new on it, it passes every word 4..7 whose stored value
(`project + 0x4080 + track × 0xe8 + slot × 4`, project `86bd027c`) differs
from the current one to stock's set (`8d5acf28`). Stock stores it and sends
it to the audio side (`8d5acd9c`, message 0xa1) as it does for any edit. This
runs once per placement, so a later parameter lock is left alone. GUI and
audio share no memory: values reach audio only through stock's messages.

Both controllers' edits, plain or locked, reach stock's encoder step
`8d5ad2e8(track, index, current, event)`. Its call at `8d5ad306` asks
`8c0b2468(track, index, &scale)` for the parameter's step count, or -1 for a
continuous one, with `scale` preset to 2. A stepped parameter moves
`scale × count` buckets, one per detent at speed 0 (`event[2]`, else two, and
more from 100 steps), from the bucket the current word falls in
(`word × buckets >> 31`), and stores the bucket's centre; the product must stay
below 256. The hook answers an owned synth's or send effect's enum slot with
its choice count and `scale` 1, so a detent selects the next choice, and passes
every other query to stock.

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
hook (`8d53b636`, the value-text call in its loop over positions) lets an owned
effect name a `division` control's value, as stock does only for its delay
(effect 0, table `0x84056aa0`).

### LFO destinations

[`lfo.cpp`](../sdk/platform/opxy-1.2.1/lfo.cpp) extends the engine destination
group of the Value, Random and Element LFOs on instrument tracks 0..7.
Generated metadata supplies each instrument's declared parameter count, encoder assignments and
record-ownership probe, without requiring its screen to be loaded. Only an
owned machine with five to eight parameters uses the extended routing.

Four calls to the stock range helper (`8d53f828`, `8d53fb18`, `8d540afa`,
`8d543280`) retain the selector's eight positions instead of folding them
into four. The live and parameter-lock quantizer calls (`8d5ad59e`,
`8d5ad4ba`) step among the declared slots and store their Q31 midpoints.
The instrument controller's click-publication call (`8c0b2d54`) cycles the
current full slot modulo the declared count before delegating to stock's
original setter. This bypasses its four-position click quantization without
changing other parameters or destination groups.
The engine-group audio call (`11a04260`) takes the full selector from R4,
decodes the same slot and delegates to stock's modulation function, which
still owns saturation and restoration. This path reads the audio engine
table and record ownership only; it does not call GUI or project getters.
It also corrects stock's destination scratch word (`84047950`) before the
existing modulation telemetry publication (`11a04628`). The GUI therefore
animates the same full parameter slot that audio modulates, while stock
retains the publication timing, amount and selected-track filtering.
Other destination groups and stock engines retain their original routing.
Three icon-strip animation calls (`8d54023e`, `8d541736`, `8d543d92`)
map slots 4..7 to their declared encoder using the caller's decoded group
and slot; sample engines repeat the four positions for their second row.
Element's name call (`8d53ffb0`) also restores the strip's
visibility for those slots. Its initializer's offset calculation (`8d53f888`)
uses the same mapping after stock stores the full slot, so reopening the page also shows the
correct icon. The full slot still selects the label and audio
destination; only the strip's scroll position is remapped.
The three name calls (`8d53ffb0`, `8d54107e`, `8d543970`) and icon mapping
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

Requirements: flag 1, an engine ID of 34–255, and page 2 or 7. The engine never
touches the sample cache or its lock.

Stock's sample predicate and note-region dispatcher also have explicit engine
ID checks. One generated table describes the collection's sample machines
([`sample_callbacks.cpp.in`](../sdk/platform/opxy-1.2.1/sample_callbacks.cpp.in));
[`sample_machines.cpp`](../sdk/platform/opxy-1.2.1/sample_machines.cpp)
admits an ID only while its registration is still ours, and an owned engine
borrows a stock payload: the sampler's on page 2, routed as the sampler's one
sample for every key, and the multisampler's on page 7. Track and voice IDs
stay unchanged. Stock,
absent and conflicting registrations follow the original paths. The first
sample machine in a build enables the `samples` service; further ones add no
firmware patch sites.

The preset writer has a separate engine-ID switch before its sample-region
serializer. The SDK aliases only that switch to the owned engine's sampler
base. The writer retains the machine's saved type and exports the sample paths,
outer and inner markers, tuning, level, flags and tagged settings. This also
preserves the live sample when stock reloads a newly saved preset.

Stock starts these voices through a separate note-start method; factory
creation alone does not start or choke a voice. Page 7 maps notes to
the stock 24 canonical pads. Optional same-key choke is scoped to track and
canonical pad, and checked before DSP or analysis lookup.

### Hooks

Hooks that replace a CALL are C++ with the C ABI. Hooks entered inside a stock
function are tiny assembly stubs: they save everything compiled C++ may
clobber through one routine pair ([`hook.s`](../sdk/platform/opxy-1.2.1/hook.s):
R0-R3, P0-P2, ASTAT, the I/M/B/L registers, both hardware loops and both
accumulators), call C++, and replay or deliver what stock expects. Stubs remain
only where the contract is not C: an argument in R5, outputs in R5/R7/P4/CC, a
return-address marker, the caller's frame, and replayed instructions.
Stock addresses used by more than one unit are in
[`stock.hpp`](../sdk/platform/opxy-1.2.1/stock.hpp); every sample record
offset is in [`stock_sample.hpp`](../sdk/platform/opxy-1.2.1/stock_sample.hpp).

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

The paired held-pad key handlers recognize owned drum-page machines as stock
drum ID 3 for slicing and pad copy/paste; the track retains its real ID. Stock
copies the complete pad record for each slice. The clipboard bridge preserves
settings within the same machine, uses destination defaults across machines,
and removes SDK tags when pasting into stock. It restores the clipboard after
each paste, including failure, so subsequent pastes retain the source settings.

The tap slicer auditions an owned drum-page source once, forwards. Machines
with `sliceTiming` supply a source clock matching frozen settings, live
parameters, track transpose and project tempo. Other machines audition raw PCM
at the original file rate. The SDK extends only the scoped source note's stock
packet with a versioned snapshot, keeping the original note prefix unchanged.
The audio receiver validates length, tag, engine ownership, track, key and range
before publishing role-local state; GUI globals are not a transport. Snapshots
are retained per track/key because stock can construct a pending voice even
after its note-off. A new ordinary manual note clears only its matching slot.

The factory matches the parent note's identity and manual provenance, then
copies the snapshot into the voice. Subsequent cuts cannot move that audition's
playhead; later control and tempo edits apply to the next audition. Scoped
source note-on/off calls preserve the source pad through stock keyboard
remapping and project transpose. Ordinary and sequencer notes retain their
normal routing and playback. No audition state is saved in a preset or project.

Pad settings occupy the stock pad record's fade word as a tagged 18-bit value,
padded to survive stock's integer/float serialization. Values up to 65535
retain the original 16-bit tag; larger values use an extended tag. Both tags
round-trip exactly and are recognized in assignment, audio and copy/paste.
For a machine with pad
settings the drum bridge owns that word: stock does not paint a fade, the
Shift encoders 2/3 never reach stock, and presses never enter stock's mode
cycle. With drum controls they reach the machine instead. Unowned
registrations delegate to stock.

The edit-multiple controller's separate turn and encoder-click paths route
through the same machine callbacks. Stock owns the selection mask. Canonical
pad indices are converted back to played notes before the octave-aware region
lookup and publisher, so edits reach the selected pads in either octave.

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
`save(project, path)` (`0x8d599074`), which `0x8d585e98` calls once, at
`0x8d585eda`, with stock's project object. That call is hooked: the SDK calls
stock `save`, and when it succeeds reads the saved file back with stock's
newlib `fopen`, `fread` and `fclose` (`0x8fed0348`, `0x8fed05bc`,
`0x8fed00ae`) for its size and CRC-32, then writes the companion beside it with
`fwrite` (`0x8fed0780`) ([`project_companion.cpp`](../sdk/platform/opxy-1.2.1/project_companion.cpp)).
The machines it lists are the collection's, from their link-time numbers; a
kit leaves out machines with number 0. A failed save writes nothing.

Stock loads every project, the workspace at startup and each project opened,
through `load_project(path, commit)` (`0x8d5890b0`). It deserializes the file
into the inactive of its two project buffers with one call,
`deserialize(project, path)` (`0x8d599b10`, at `0x8d58913e`), then validates
the buffer (`0x8d589024`) and makes it pending. The buffer is the inactive one,
so no other reader should see it mid-edit (inferred from the buffer selection,
not traced end to end). That call is hooked: the SDK calls stock `deserialize`, and when it
succeeds opens the `.oxy` beside the `.xy`, measures the `.xy` as the writer
does, and uses the companion only when its binding matches, its
`open-xy.machines` section is version 1 for `opxy-1.2.1`, and the whole file
is valid JSON of at most 64 KiB ([format](projects.md#format)). The reader
streams the file through a 4 KiB buffer and keeps no document; its
[limits](projects.md#reader-limits) are part of the format. Any problem
leaves the project as saved, and the load never fails because of its
companion.

A project holds its machine per pattern: track `t` (0–15) starts at
`project + 0xd7c + t * 0x53f44`, its pattern count is the word at track
`+ 0x53f40`, pattern `k` starts at track `+ k * 0x53f4`, and the pattern's
machine number is the word at pattern `+ 0x18`, the one stock's track-engine
getter reads. Instrument tracks 0–7 hold engine numbers there and send tracks
14 and 15 effect numbers; tracks 8–13 hold none. Projects store no page number.
The reader maps every pattern's number on tracks 0–7 through the companion's
engines and on 14–15 through its effects, all at once, so two machines can
trade numbers. A number the companion does not list, a stock machine's, stays.

A listed machine this firmware lacks (absent, left out of a kit, or now the
other kind) gets stock's own fallback written into every pattern: Prism (18)
on an instrument track and no effect (0) on a send. No pattern is left with a
number stock would have to correct, and none can open the machine this
firmware's layout gave the old number. A number two listed machines claim is
treated the same way, and a machine listed twice counts once, by its last
entry, as in `tooling/projects.py`. The firmware shows no notice for a missing
machine yet.

Stock's MTP view lists only paths matching a table of regular expressions
(`0x840ae0c4`…), and under `/projects` only `.xy`. Three text edits rewrite
the workspace and project patterns in place to `\.o?xy$`. Dropping a redundant
`\d` (already in `\w`) from one character class keeps each string's length;
the workspace pattern uses its padding.

## Presets

At boot, stock scans `content/presets` (factory presets, which the device
never deletes or moves) and then `/presets` (the user's). For each preset it
reads the top-level `"type"` at `8d4eed56`, and lists only presets with one.
The SDK hook ([`presets.cpp`](../sdk/platform/opxy-1.2.1/presets.cpp)) calls
the stock reader unchanged and then, on the factory pass, empties the type
when no engine of that name is installed: neither stock's resolver knows it
nor is it one of the build's machines. Such a preset, for example the default
preset of a machine a later build left out, is then not listed; its file stays
on the device. Until stock's engine name map exists, every name counts as
installed. User presets stay listed: stock deletes a user folder that lists no
presets, so hiding them could delete them.

## Send effects

Effects live in a separate registry (19 stock entries, 63 relocated). Stock
registers send effects and the per-track filter types in it; the filter setter
keeps the stock bound, so IDs above 18 are send effects only. Stock firmware's
send-slot setter (`0x8d50e740`) rejects types above 18. The audio mixer keeps one
**480-byte** slot per track (`0x84c66b9c + 480 * slot`); the two sends are
slots 14 and 15, and either may hold any effect, including the same one. When a
send's effect changes, the old object fades out over a few blocks and only
then is the new one constructed in place, so a slot never holds two live
effects. The mixer calls `init(object, slot)` after construction and again on
an engine reset, then `process(left, right, parameters)` once per 64-sample
block on in-place stereo buffers. **No destructor ever runs**, so an effect may
hold no resources. The first slot word is the object's method table, which
leaves 476 bytes of effect state.

A send track reports its effect ID where an instrument track reports its
engine, and stock's parameter stepping (`0x8d5ad254`) treats that ID as an
engine. On the first four controls, ID 2 gets engine 2's eighth-size steps,
which round a detent to nothing, and IDs 3 and 4 get pitch stepping in cents.
Other stock paths test engines 6 and 29–31, and split at 29. Effects
therefore use only free IDs that no stock engine holds and stock never tests:
1, 8, 11, 21, 23–28 and 34–62, never the stock "none" engine 33. For the same
reason a layout never gives an effect and an engine the same number, so an SDK
hook that asks about an engine number never mistakes a send track for an SDK
engine.

Stock delay, reverb and chorus keep their delay lines in a static per-send
scratchpad, two 1 MiB regions that `scratch(slot, bytes)` (`0x8d50e048`)
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
