# Master processor extensions

An extension may replace the master EQ with one stereo processor. Stock EQ
remains the other choice in the shared stock `filter` selector, opened with
Shift+M2 on MIX page 2. Left-encoder navigation and left/white click confirmation
reuse the stock selector adapter. One included extension may own this capability.

Declare `"master_filter": {"artwork": "artwork/my_processor.svg"}` in
`extension.json`. Optional `state_id` keeps project identity stable across an
extension rename; it defaults to the extension ID and cannot be `eq`.
DSP and artwork live in the extension repository, not in the SDK.

## Parameters and audio

Supply `definition.json` using the shared definition schema: `schema`, `id`,
`name`, `parameters`, `presets`, and `resources.display`. There are 1–8 parameters,
using the same IDs, normalized defaults, mapping, display formats and optional
alternate `control` positions as effects. There are four encoder positions;
clicking a position with alternates selects its next parameter, otherwise it
resets the shown parameter to its declared default. Enum turns select one choice
per detent. A project change resets alternate selection to its first positions.

Export `opxy::extensions::<id>::hooks` and `opxy::extensions::<id>::filter`, an
immutable `opxy::master::Filter` from `<opxy/master_filter.hpp>`:

| Callback | Audio-role contract |
| --- | --- |
| `reset()` | Initialize private audio state on the first audio block |
| `prepare(opxy::Parameters)` | Read eight normalized Q31 slots before each block; undeclared slots are zero |
| `process(left,right)` | Transform 64 stereo Q31 samples at 44.1 kHz in place |

Audio callbacks never allocate, block, perform I/O, draw or register anything.
Complete parameter sets cross the stock B3 transport and become visible to audio
only after a matching commit. The SDK owns the 20 ms stock/extension crossfade.
No sweep, neutral position, filter algorithm or control count is built into the
processor interface. The old sweep encoding is accepted only for `.oxy` migration.

## Display

Use `display.h`, `display.cpp`, `DisplayState`, `display_reset` and `display`
with the existing [`draw::Frame` and `draw::Input`](display.md). The normal display
binding validates sizes and frame layout; the same retained renderer binds and
applies paths, nodes, text and palettes for instruments and master processors.
The master host loads the authored standalone 480×222 SVG in unregistered mode
(screen ID zero), preserving stock EQ's screen slot. It never accesses audio state.

Unlike a synth, this standalone screen does not contain the stock header
template. To show control captions, author the optional header text leaves
described in the shared [display contract](display.md#frame). The shared header
adapter supplies declared labels and normalized amounts for the currently shown
parameters, and applies `Frame.header` overrides for real units or enums. Artwork
without these leaves remains full-screen, with no header inserted. The existing
`draw::page_header` helper and normal text slots are also available for custom
two-row headers; no master-specific drawing interface is needed.

All normal display limits apply: at most four closed mutable outlines, 64 points
per outline, 256 nodes, 16 texts and 16 palette entries. Mutable points are local
coordinates within ±64 pixels; position and scale their nodes normally. Do not
pass collapsed filled shapes to stock; hide them while their area is zero.
Artwork uses the same numeric, paint, binding and reference safety checks as
instrument artwork, including at most four fractional digits and no exponents. Element names must
be unprefixed (`<svg>`, `<path>`); use the default SVG namespace, never
`<ns0:svg>` or `<svg:path>`, which stock does not recognize.

The optional shared GUI callback `display_parameter_reset(DisplayState&, u32)`
receives an explicit reset of a declared parameter. It can start an animation
from existing display state. It does not run on audio. Existing displays need not
implement it, and `draw::Input` and the browser drawing ABI are unchanged.
Display state resets on project activation; the next frame carries every slot
and the parameter index shown at each encoder position.

## Projects and packaging

The SDK saves processor identity and parameters in
[`open-xy.master-filter`](projects.md#open-xymaster-filter), retaining the shared
streaming companion reader and machine remapping. State becomes active only at
project publication. Missing/unusable state selects stock EQ and declared
parameter defaults, preserving stock EQ's own values.

Native builds install the authored SVG. Kits include its source hash and install
it only when its owner is selected. Without an owner, capability hooks remain
stock, service objects and artwork are omitted, and the shared companion support
remains available. Addresses and native adapters belong to the SDK; audio,
geometry, artwork generators and sound tests belong to the extension.
