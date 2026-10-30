# Master-filter extensions

An extension can replace the master EQ with one stereo processor and one
control, while retaining stock EQ as the other choice. It lives in its own
repository. Declare the capability in `extension.json`:

```json
"master_filter": {"artwork": "artwork/my_filter.svg"}
```

An optional `state_id` in `master_filter` keeps the serialized processor ID
stable when the extension is renamed. It defaults to the extension ID, uses
the same identifier syntax and cannot be `eq` (reserved for stock EQ).

Export `opxy::extensions::<id>::hooks` (an empty `Hooks()` is sufficient) and
`opxy::extensions::<id>::filter`, a constant `opxy::master::Filter` from
[`master_filter.hpp`](../sdk/include/opxy/master_filter.hpp). Its selector label
is 1–15 characters. The collection accepts one master-filter owner. Existing
controller observers and their claims/actions are unchanged.

The SDK owns Shift+M2 on MIX page 2, the shared stock `filter` selector, the
first encoder's sweep and neutral reset, parameter transport and a 20 ms
crossfade between stock EQ and the extension. Left-encoder navigation and
left/white click confirmation use the existing stock selector adapter.

| Callback | Stage and thread | Contract |
| --- | --- | --- |
| `reset()` | First audio block, audio role | Initialize audio state without allocation |
| `prepare(sweep)` | Before each processed block, audio role | Sweep 0–1023, neutral 512 |
| `process(left,right)` | Master EQ position, audio role | Transform 64 stereo Q31 samples at 44.1 kHz in place |
| `panel.frame(scene,input)` | Master filter display frame, GUI role | Update borrowed scene; never access audio state |

Audio callbacks never draw, perform I/O, allocate, block or register anything.
GUI and audio state must be separate; the SDK sends complete control snapshots
through stock's B3 transport. The immutable generated descriptor accessor does
not initialize or dispatch the controller extension runtime. Existing queued
controls are delivered after stock handling on the controller path, so they
cannot provide this audio transform or replace the master page's earlier
Shift routing. This capability therefore has its own conditional profile
service rather than adding a second observer of an existing recording stage.

A panel is a standalone, authored 480×222 path-only SVG, at most 64 KiB. The
manifest determines its installed filename. Its descriptor names up to eight
mutable paths, each with 2–32 points and a fixed open/closed topology, plus a
grayscale target and donor prefix with up to 256 entries. `Scene.path` accepts
Q10 screen coordinates bounded to ±1024 pixels. Hide collapsed polygons;
keep their last valid geometry. `Scene.gray` selects a cached donor fill.
The adapter binds and validates the SVG once and keeps it unregistered
(screen ID zero); it never occupies stock EQ's screen slot. Scene calls work
only during the synchronous GUI callback. `Input` carries elapsed Q10 seconds
and project/reset/turn serials, allowing the extension to animate reset.

The SDK saves the selected processor ID and sweep in
[`open-xy.master-filter`](projects.md#open-xymaster-filter). The existing
streaming companion reader still remaps machines before stock validation;
filter state becomes active only at project publication. Missing or unusable
state selects stock EQ and neutral sweep, preserving stock EQ parameters.

Native builds install the authored SVG. Kits include its source hash and add
it only when its owner is selected. With no master owner, the master-filter
service objects are omitted and all its stock hook sites remain unchanged.
A small shared state/collection unit supports the existing companion path.
Firmware addresses, trampoline bytes and drawing/storage ABI belong to the
SDK adapter; DSP, artwork, generators and sound tests belong to the extension.
