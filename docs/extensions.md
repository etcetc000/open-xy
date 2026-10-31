# Firmware extensions

Extensions change how the OP-XY itself behaves: recording, controls, the
sequencer. Machines make sound and draw; extensions observe the firmware's
events and act through stock. Like machines, extensions live in repositories of
their own and reach builds through the catalog
([`catalog/extensions.json`](../catalog/extensions.json)). Capture is the first,
in [open-xy-core-machines](https://github.com/etcetc000/open-xy-core-machines/tree/main/extensions/capture), and the worked example for the next.

An extension is ordinary C++11 compiled into the firmware. It has no firmware
addresses, assembly, platform conditionals or OS headers. The SDK installs each
authenticated hook once and forwards stock input unchanged. This interface is
experimental and source-level; it is not a dynamic loader or binary ABI.

## How the SDK exposes the firmware

Three layers, kept apart:

1. **Patch sites** are private and belong to one OS release: the hooks in
   [`profiles/opxy-1.2.1.json`](../profiles/opxy-1.2.1.json). The SDK owns every
   site, each exactly once; the build rejects two edits that overlap. No
   extension or machine patches the firmware.
2. **Events** are public and outlive an OS release: the callbacks of
   [`Hooks`](../sdk/include/opxy/extension.hpp). Each extension hook in the
   profile lists the events it feeds (`events`); the build checks every hook
   names known events and every event has a site. An OS port moves the sites;
   the events stay.
3. **Subscribers** are extensions and, later, machines. They take events and
   filter them; they never see a site.

### Name an event by where it happens, and by its scope

"Note on" is ambiguous: a note passes several stages, each its own event.

| Stage | Happens when | Event |
| --- | --- | --- |
| Input | A key or MIDI note arrives, before recording and the sequencer | `recordable_note_on` and `selected_note_off` (selected track only); `input_activity` (any track, but without the note) |
| Sequencer | A pattern step plays | Not exposed |
| Track | Live and sequenced notes reach a track, before its instrument | Not exposed |
| Voice | The instrument starts a voice | A machine's voice callbacks |

An event's name says what it delivers. `recordable_note_on` is not every note
on: it is a live onset stock accepted for recording, on the selected track,
while playing. A subscriber that wants a narrower scope filters; one that needs
a wider scope widens the event (below), never adds a nearby site. The same
holds for time (transport, GUI tick), controls (queued keys, keyboard keys,
encoder edits, page keys) and the project lifecycle (loading, starting a new
project, saving); of these, only queued keys are exposed.

Adjacent sites are still separate sites. The controller update behind `context`
follows stock's GUI tick in the same main loop; a GUI-tick event would be a site
of its own beside it.

### Access: observe, transform or claim

| Access | Rule | Here |
| --- | --- | --- |
| Observe | Read-only; every subscriber, in collection order | Every event |
| Transform | One ordered chain that may change or drop the event | None yet |
| Claim | One owner per build; a second claimant fails the build | `record_double_tap` |

### Thread and host

Each event runs on one thread: audio, GUI and controller, or project load and
save. All current events run on the controller path. A subscriber is either
firmware-wide, as an extension, or hosted by a machine on a track, which
filters to that track for it.

### Four kinds of interface

| Kind | What | Here |
| --- | --- | --- |
| Events | Something happened | The `Hooks` callbacks |
| Actions | Change something through stock | `Actions::insert_notes`, `Actions::replace_notes`, `Actions::notify` |
| Surfaces | Add to the screen | None for extensions yet: menu rows, pages, overlays |
| Saved state | Data kept with a project | None for extensions yet |

An event says something happened; an action asks stock to do something. Sending
a note to a track, clearing a pattern or changing a parameter is an action, not
an event. A feature that needs a kind with nothing here yet (a setting the user
chooses, saved state, note input for effect machines) starts with an SDK pull
request for that interface, which the feature then uses.

## Events

Callbacks run synchronously on the controller path, in collection order. Keep
them bounded: no allocation, blocking, I/O or logging. Borrow event references
only for the callback. The SDK does not sandbox native code.

| Event | Delivers | Sites |
| --- | --- | --- |
| `context` | Project, pattern, selected track, length, scale, quantization and play state, and whether its pattern is `editable` and its live notes `recordable` (not while the recorder is in a step or sample workflow); before every other event | Controller update, Stop |
| `recordable_note_on` | A live onset stock accepted for recording, on the selected recordable instrument track, while playing; with stock's pickup position and collision radius | Live onset, accepted-note bookkeeping |
| `selected_note_off` | A live release on the selected track, playing or not; may have no preceding onset | Live release |
| `recording_tick` | The selected track's scaled Q8 recording time, while playing and recordable; a discontinuity asks the subscriber to discard timing | Recorder tick |
| `input_activity` | Any live note input, accepted or not, on any track; for cancelling gestures | Live onset |
| `queued_control` | A queued key (Record and the other queued controls; not keyboard keys 26–49) after stock handled it, with a microsecond time. `accepted`: stock took it as input (for Record, stock registered the press or release); `unmodified`: no other key or Shift held. Key numbers are in `keys` | Key queue |

Project and pattern IDs are opaque equality tokens. Lengths use pattern ticks;
scale 1 has 480 ticks per step. Stock's pickup gives negative positions near
pattern zero. Quantization belongs to stock playback; subscribers must not
invent a second grid conversion. Sequenced notes and step entry are not live
onsets.

## Claims and actions

[`profiles/extensions.json`](../profiles/extensions.json) holds the registry: the
SDK's claims and actions. Each extension's `extension.json` lists those it uses.

- **A claim** is an exclusive control on one key: a `label` the site shows and
  the `key` it governs. One owner per build; two extensions with the same claim
  cannot be built together, and the site will not offer both. The owner
  recognizes its gesture itself; the shared
  [`DoubleTap`](../sdk/include/opxy/double_tap.hpp) helper handles taps, holds,
  repeats and timeouts on any key without delaying stock controls.
  `record_double_tap` is capture's.
- **An action** is permission to change stock state. The runtime grants an
  extension's actions inside its `queued_control` callback for a key it claims,
  once stock accepted the event; an action's `requires` limits that to one
  claim, and without it any claim its holder owns grants it.
  - `edit_notes` (requires `record_double_tap`) unlocks `Actions::insert_notes`
    (add beside the existing notes) and `Actions::replace_notes` (clear the
    track's notes, then add), on any instrument track whose pattern is
    editable. The SDK checks the caller's context is still current, validates
    the bounded batch, opens one stock undo transaction, keeps stock collision
    and capacity behavior, returns the inserted prefix length and publishes
    once. Replacing is refused, changing nothing, while stock is still
    recording a held note there.
  - `notify` unlocks `Actions::notify`: one short popup per callback.

  Calls outside the callback, with a stale context, or a second write or
  popup in one callback are refused.

Observers cannot transform notes or suppress stock input.

## Add an extension

An extension is a folder in a repository of its own, beside this SDK checkout:

1. `extension.json`: its `id`, the site's row and details panel (a `name`, a
   one-line `description` saying what it does and how to use it, 140 characters
   at most, and optionally a `note` for its one limit, 100), and the `claims`
   and `actions` it uses from the registry. People scan this; put the full
   behavior in your repository's README.
2. `extension.cpp`, exporting `opxy::extensions::<id>::hooks`, naming only the
   events it takes:
   `extern constexpr Hooks hooks = Hooks().with_context(context).with_queued_control(queued_control);`
   Keep its helpers in the folder.
3. `tests/`: host tests (below). An extension without them is refused.

Check it with `python ../open-xy/sdk.py test --extension <folder>`, and build it
into your own firmware with `--extension <folder>` beside `--machine` on
`compile`, `build` and `kit`. To offer it in the builder, tag a release and open
a pull request that adds its entry to `catalog/extensions.json`: the same
format as [machines](catalog.md#entries), with `"kind": "extension"` and no
`cpu`. Origin follows the catalog: Open-XY's own (`catalog.OFFICIAL_EXTENSIONS`)
show as Core, others as Community, and folders outside the catalog as Yours.

A build's collection lists its extensions after its machines; that order is the
callback order. Native builds include the whole collection. The site's Extensions step follows
Machines, starts with none selected, saves choices with the layout and omits
unselected extensions entirely. The SDK's extension hooks form the profile's
`extensions` service: a build with no extension selected links none of the
runtime and leaves every one of its stock call sites unchanged.

## Change the SDK for an extension

Reuse first: read the events table. Most needs are a subscriber that filters an
existing event.

**Widen an event** when it fires too rarely for you: a track other than the
selected one, notes while stopped, keyboard keys. Relax the filter at the
existing site and add fields to its event struct (`LiveNote`, `Control`) that
let every current subscriber keep its behavior: a `selected` or `recordable`
flag it now tests. Do not add a second site for the same stage.

If the event's name stops being true, rename it, and change its parameters if
needed, in the same pull request. Extensions build from source in this
repository, so update every subscriber there, and the profile's `events` tags.
Prefer a new field in the event struct to a new callback parameter. For
example, notes from every track, playing or stopped, widen the onset event to
an `input_note_on` that carries `selected` and `recordable`; capture tests
`recordable`.

**Add an event** only for a stage, thread or kind no event covers: a member of
`Hooks`, its parameter in the full constructor and its `with_` builder (the
build reads the event list from the builders). The pull request states:

- the event's name, by what it delivers and its scope;
- its stage, access (observe, transform or claim), thread and host;
- each firmware site, tagged with `events` and the `extensions` service, and
  how it was found and verified in the stock OS;
- why widening an existing event would not serve.

Extensions name their callbacks, so adding one changes none of them. Renaming
or widening one updates every extension in the same pull request.

**Add a claim** with one registry entry in [`profiles/extensions.json`](../profiles/extensions.json): its
`label` and `key`. Nothing else changes; the runtime reads claims from the
generated registry. Never add a per-claim field to the collection or a
per-claim branch to the runtime.

**Add an action** with a registry entry (and its `requires`, if one claim must
grant it), a bit in
[`extension_runtime.hpp`](../sdk/platform/extension_runtime.hpp) in registry
order (a test keeps them in step), its function appended to `Actions`, and its
stock work in the version adapter,
[`extension_stock.cpp`](../sdk/platform/opxy-1.2.1/extension_stock.cpp), never
in the extension. The runtime already decides when it is granted. An action that drives a stock path the SDK hooks (playing a
note reaches the live-note sites) says how it avoids re-entry: events raised
during a callback are dropped. An action that changes stock state uses one
stock undo transaction. A claim exists only where stock behavior would
otherwise conflict: say what stock already does with that gesture.

## Verify

An extension's tests are its `tests/*.cpp`: each is a host program that returns
0 when its checks pass, built with the SDK headers and the extension's folder
on the include path. Drive the callbacks as the runtime does, with a fake
`Actions` table for the writes (capture's
[`gesture.cpp`](https://github.com/etcetc000/open-xy-core-machines/tree/main/extensions/capture/tests/gesture.cpp) does). `sdk.py test --extension`
and `sdk.py catalog test` run them.

For SDK changes, run `python sdk.py check`, then the full
[native release verification](build.md#release-verification). The SDK's own
tests exercise callback ordering, exclusive claims, the registry, grants from
data, every hook's events, stale-context and unauthorized edits, and the
adapter end to end with stock boundary doubles and the SDK's probe extension
([`tests/fixtures/extension_probe`](../tests/fixtures/extension_probe/extension.cpp)). Device scheduling still requires
testing the exact built image.

### Read a stopped pattern source

An extension with the `read_patterns` action can inspect an existing instrument
pattern inside its accepted, claimed `queued_control` callback. These read actions
do not change the selected scene, create undo entries or publish edits.

```cpp
opxy::extensions::PatternInfo source{};
opxy::extensions::Note notes[opxy::extensions::pattern_note_capacity];
opxy::u32 count = 0;
if (actions.discover_pattern(2, 1, source) &&
    actions.read_pattern(source.ref, notes, opxy::extensions::pattern_note_capacity, count)) {
    // Track 3, source B, independently of the scene's selected pattern.
    // count == 0 is a successful empty pattern.
}
```

Tracks are zero-based 0–7 and source indices 0–15; only existing sources can be
read. `PatternInfo` supplies length in stock ticks (480 per step at scale 1) and
the stock scale index. `PatternRef` is an equality-only project-generation-bound
identity, not a pointer or persistent identifier. Discover it again after loading
or creating a project, including when stock reuses the same memory address.

Reads require stopped transport, no held or latched Record, normal recorder mode
and no pending project load. They copy and validate the bounded native note list;
held notes and malformed lists fail. This is a stopped controller-path inspection
service, not an atomic live editor subscription. `read_pattern` copies all notes
or none: insufficient capacity returns false with count zero and leaves the
output buffer unchanged. The maximum capacity is 120. The actions are unavailable
outside the granted callback, including through a retained `Actions` value.


For a deferred read, register `with_pattern_read_result(callback)` and call
`actions.request_pattern_read(ref)` in a granted control callback. A nonzero
return value is its request ID; zero means unavailable or another snapshot is
outstanding. The GUI service performs the bounded copy outside stock key and
render callbacks. The owning extension receives a `PatternReadResult` on the
controller path, after its context callback. No contributor code runs on the GUI
service. The result's notes are borrowed only for that callback. Failure carries
no notes; a changed project or no-longer-readable context fails rather than
returning data from the old project. Do not wait or spin for completion.

The SDK holds one snapshot until its result callback returns. Another request
cannot replace it, including from another extension. This service reads existing
sources; it does not allocate, clone or edit patterns.

## Owning the Arrange surface

An extension can replace the stock Song page while its `enabled` callback is
true. Declare `"arrangement": true` and the exclusive `arrangement_surface`
claim in `extension.json`, include `<opxy/arrangement.hpp>`, and attach a
`const ArrangementSurface` with `Hooks().surface(&surface)`. Only one owner
can be included. A disabled, omitted or failed initialized surface leaves the
stock Song page; the SDK never changes the selected instrument on entry.

The SDK supplies neutral SVG artwork and the existing retained renderer for
`ArrangementFrame` (`128` four-point paths, `128` nodes, `80` texts and `16`
grayscale palette entries). All existing coordinate, transform and frame
validation rules apply. Path keys must be nonzero and change when geometry
changes. The generated screen registers in a vacant relocated page slot;
its validated SVG markup, renderer and state belong to the selected extension.
The SVG is embedded and parsed in memory during GUI initialization, so no
external surface file can be missing or collide with machine artwork. Omitting that
extension omits the frame storage too. This is a drawing and input service,
not an arranger, project format or playback engine. Mutable paths are outlines;
this does not add arbitrary filled geometry. It lets an extension such
as Linear draw clips and a cursor without writing private SVG objects.

Input ownership is declared with three key-mask words and a four-bit encoder
mask. `ArrangementKey` names supported physical controls. Keyboard keys26–49 are not capturable: their immediate input path is outside
the queued controller producer. A mask containing them disables the surface.
Unclaimed keys and turns reach stock unchanged. The SDK captures a complete press/release pair
before stock held-key bookkeeping; releases for presses already forwarded to
stock always stay stock. Repeated captured presses are suppressed. A full
queue forwards a new press instead of stealing it; reserved slots ensure the
releases of captured presses still fit. Encoder queue overflow forwards that
turn. Choose masks deliberately: capturing Play or Record prevents
their stock behavior and does not itself implement a replacement.

The controller task decides ownership and copies input into a bounded queue.
It never calls the extension's GUI callbacks. The GUI poll drains at most
32 events from a 256-entry queue per iteration, discarding old-project events.
`key`, `turn`, `context`, `tick`, `reply` and `render` callbacks all execute on
the GUI task. `render` receives the frame by reference; `presented` advances
only after a valid frame reaches the stock drawing call. GUI transactions can
wait for that counter in `tick`; no writes should run inside rendering.
Visibility requires initialized artwork and the Arrange controller, including
when another page was selected since the previous GUI poll.

A GUI key/turn/tick callback receives `ArrangementActions::submit`: it copies
an application-defined code and up to four words into **one pending intent**.
False means busy or outside the permitted callback. An intent carries its
opaque project generation/identity and selected track. On the controller task,
the SDK revalidates its project and calls only that owner's `intent` callback
with its declared `Actions`, excluding native `insert_notes`/`replace_notes`.
This permits deferred pattern requests without running project work during
input or painting. Do not share mutable GUI state with this callback.

For responses, declare `surface_reply` and use
`Actions::post_arrangement_reply(origin, bytes, size)`. The SDK copies at most
**2048 bytes** into **one pending reply**; busy, oversized or stale-project
responses fail without overwriting the prior reply. The GUI `reply` callback
borrows those copied bytes only for its duration. `pattern_result`, when
supplied on the surface, runs on the controller after an owned deferred
pattern request and receives only this reply permission. The existing
`Hooks::pattern_read_result` observer remains grantless. Stored `Actions`
cannot be used after their callback. Use separate controller-owned request
state and GUI-owned editor state, connected only by these copied messages.

The SDK fixture `tests/fixtures/arrangement_probe` demonstrates a captured
control requesting a stopped pattern snapshot and returning note bytes to
the GUI. Host tests cover ownership, queue exhaustion, generation checks and
action scopes; native compilation is not hardware timing or acceptance.

If stock clears or replaces an already constructed surface slot during a later
initializer pass, the SDK fails closed instead of reconstructing a possibly
live loader or claiming an occupied slot. That surface remains unavailable
until restart.
### Per-project extension state

An extension can declare one owned section in `extension.json`:

```json
"project_state": {"namespace": "your-owner.timeline", "version": 1, "capacity": 4096}
```

Use your GitHub owner's namespace, not `open-xy.*`. Names are at most 64
characters, with one dot separating nonempty lowercase letters, digits or
hyphens. A collection cannot repeat a namespace. Capacity is 1–8192 payload
bytes per extension and at most 16384 declared bytes across the collection.
The SDK owns the storage and native file operations.

Register `Hooks().with_project_state(on_project_state)`. Controller sync sends
`ProjectState` before the ordinary context callback when the project, payload or
save status changes. This also happens at startup and on project changes,
without pressing a claimed key. Its `token` is an opaque, temporary project
identity. Copy the borrowed `data` bytes during the callback; do not retain its
pointer. The callback runs with a nonblocking SDK state lock held; reentrant
state operations fail rather than wait. Native file workers never call extension
code. No allocation, I/O or blocking is allowed in callbacks.

`status` is `empty` for a project without this section, `ready` for readable
payload bytes, `unsupported` for a different section version, `unavailable` for
invalid state, or `save_failed` when publication failed. A save failure can still
include the current readable payload. Unsupported sections remain preserved;
they cannot be overwritten or migrated through this API. The SDK does not
interpret a payload's internal format.

To edit, declare the `project_state` action and call
`actions.publish_project_state(token, bytes, size)` from an accepted claimed
`queued_control` callback or its owned `ArrangementSurface.intent` callback. This replaces only the caller's section and marks the
native project dirty, so persistence does not depend on a later note edit. A
zero-byte payload is valid. A stale token, pending load, busy state service,
invalid payload section, unsupported version or exceeded capacity returns false
without publishing. Retained `Actions` grant no permission outside the callback.
For an Arrange surface, GUI key/turn/tick callbacks submit a copied intent;
the controller intent callback publishes its owner section and can post a copied
reply to the GUI. Pattern-result callbacks receive only reply permission and
cannot publish project state.
Use the copied state on the controller context. A GUI renderer needs an SDK
snapshot/reply handoff across task contexts; directly sharing a mutable global
copy with the renderer is not a safe snapshot.

Persistence uses the shared [project companion](projects.md), including
workspace autosave. State follows the committed source project during Save As;
it never inherits sections from the destination's previous project. New resets
state even when native project memory is reused. A validated committed load is
available to immediate workspace saves before any extension callback runs.
