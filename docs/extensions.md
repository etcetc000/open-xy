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

## Master filter

A [master-filter extension](master-filter.md) supplies a stereo audio transform
and an authored panel through the SDK's exclusive master-filter capability.
It uses the stock selector and the project companion service; it does not
intercept the controller recording events listed above.

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
