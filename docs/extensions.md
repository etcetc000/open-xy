# Firmware extensions

Extensions change controller and sequencer behavior. Machines implement sound
and display callbacks; extensions receive selected-track context, accepted live
notes, releases, scaled recording ticks and controls. Capture is the first
extension, in [`extensions/capture/`](../extensions/capture/extension.cpp).

An extension is ordinary C++11 compiled into the firmware. It has no firmware
addresses, assembly, platform conditionals or OS headers. The SDK installs each
authenticated hook once and forwards stock input unchanged. This interface is
experimental and source-level; it is not a dynamic loader or binary ABI.

## Add an extension

Create `extensions/<id>/extension.cpp`. Export
`opxy::extensions::<id>::hooks`, a constant
[`Hooks`](../sdk/include/opxy/extension.hpp) descriptor; unused callbacks are
null. Put its implementation and private helpers in that directory. Add its ID
to [`profiles/extensions.json`](../profiles/extensions.json), with
`record_double_tap` and `edit_notes` booleans. The builder compiles the selected
extensions, validates their source boundary and records their sources and
selection in package provenance. Browser kits include these linked objects.

The array defines callback order. All observers receive the same immutable
event in that order. There is no priority guessing or registration during
playback. Duplicate IDs, multiple Record-double-tap owners and note editing
without that claim fail the build. An empty collection is valid.

## Events and ownership

Callbacks run synchronously on the controller path. Keep them bounded: no
allocation, blocking, I/O or logging. Borrow event references only for the
callback. The SDK does not sandbox native code.

| Callback | Meaning |
| --- | --- |
| `context` | Current project, pattern, selected track, length, scale, quantization and play state; delivered before note, tick and control events |
| `note_on` | Stock-accepted live note on the selected eligible instrument track while playing, with stock pickup position and collision radius |
| `note_off` | Selected-track live release; may arrive without a preceding accepted onset |
| `tick` | Stock-scaled Q8 duration delta; a discontinuity asks the extension to discard timing-dependent state |
| `activity` | Live input activity, including rejected notes; useful for cancelling gestures |
| `control` | Queued control after stock handling, with a 64-bit microsecond time; `record_accepted` and `unmodified_record` describe Record specifically |

Project and pattern IDs are opaque equality tokens. Lengths use pattern ticks;
scale 1 has 480 ticks per step. The adapter supplies negative pickup positions
near pattern zero. Quantization belongs to stock playback; extensions must not
invent a second grid conversion. Generated sequencer notes and step entry do
not become live onsets.

Observers cannot transform notes or suppress stock input. The only current
exclusive command is Record double-tap. Its owner recognizes the gesture; the
shared [`RecordTap`](../sdk/include/opxy/record_tap.hpp) helper handles short
taps, holds, repeats and timeouts without delaying stock controls.

Only the declared note editor can use `Actions::insert_notes`, during an
accepted Record control callback. The SDK rechecks context and validates the
bounded batch before opening one stock undo transaction. It preserves stock
collision and capacity behavior, returns the inserted prefix length and
publishes once. A successful edit permits one bounded success notification.
Calls outside that callback, from observers, after a context change, or a
second edit within the same callback are rejected.

These phases keep observation independent of pattern edits. Changing live
recording policy or transforming note-on/off pairs requires an explicit SDK
contract; an observer must not add its own firmware patch or write stock memory.

## Verify

Run `python sdk.py check`, then the full [native release verification](build.md#release-verification).
Shared tests exercise callback ordering, exclusive claims, stale-context and
unauthorized edits, plus capture through the actual adapter with stock boundary
doubles. Device scheduling still requires testing the exact built image.
