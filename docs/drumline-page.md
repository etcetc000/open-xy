# Drumline-page extensions

An extension may give stock Drumline (engine 4) a second page of per-key
settings, on every Drumline track of the firmware. A press of any encoder on
Drumline's engine page switches between stock's page and this one; stock's own
press does nothing there. The page is kept per track. Shift stays stock's one
row on either page (TYPE, RANDOM, PAN, VOLUME), and Shift with a press of the
first encoder still cycles the play mode.

The extension declares the page's parameters and supplies a processor that runs
on each stock Drumline voice. The SDK owns everything else:

- every firmware site;
- the page switch, the turns and the page's texts;
- the per-key storage, saved with the project and copied with the pad and the
  pattern;
- the per-hit latch: each voice keeps its key's settings from its first block,
  so a later hit or edit of the key doesn't reach a voice already sounding;
- per-key FX I and FX II sends, mixed as a `padMix` key's;
- step locks.

One extension per build supplies the page. Two can't be built together, and
the builder offers the page like a [claim](extensions.md#claims-and-actions).
Without one, nothing of it is linked and Drumline is stock's.

## Declare the page

In `extension.json`, beside the usual fields:

```json
"drumline_page": {
  "parameters": [
    {"label": "filter", "choices": ["L150", "...", "OPEN", "...", "H4.0k"], "default": 16},
    {"label": "srr", "choices": ["OFF", "22k", "...", "689"]},
    {"label": "fx i", "role": "send1"},
    {"label": "fx ii", "role": "send2"}
  ]
}
```

- One to four parameters, on encoders 1 to 4 in order. An encoder past them
  shows nothing and does nothing.
- Each is an enum: a `label` (the title) and 2 to 32 `choices` (the captions),
  each 1 to 7 printable ASCII characters. Measure them in the device font: a
  value has about 53 pixels. `default` is a choice index, 0 when omitted. A turn
  moves one choice a detent.
- A parameter with `"role": "send1"` or `"send2"` has no choices of its own: it
  is the key's FX I or FX II send, with the SDK's choices `TRK` (follow the
  track's send, the default) and `0` to `100`. Each send appears once. The SDK
  mixes it; the processor ignores it.
- Each parameter needs the bits for its choices (a send 3), and all of them
  together at most 16.

Saved pads carry an owner tag made from the extension's ID. To rename the
extension and keep saved settings, add `"state_id"` with the old ID beside
`"parameters"`.

The layout is part of every saved project that uses it. Keep a released
extension's parameters, their order and their choices' meaning; add choices
only at the end of a list, and keep each default.

## Process the sound

Export `opxy::extensions::<id>::drumline_processor`, an immutable
`opxy::drumline::Processor` from
[`<opxy/drumline_page.hpp>`](../sdk/include/opxy/drumline_page.hpp), beside
`hooks` (which may take no events):

| Callback | Audio-role contract |
| --- | --- |
| `start(Voice&, const Choices&)` | A hit's first block, its `Voice` zeroed |
| `process(Voice&, const Choices&, i32* block)` | Each block of the hit: transform 64 mono Q31 samples at 44.1 kHz in place, after stock's level and envelope and before its pan |

`Choices` holds each parameter's choice index in declaration order, as the key
held them when the hit started, a step's locks included. `Voice` is eight words
the SDK keeps per stock voice (27 of them).

Neither callback runs for a hit whose own parameters (not the sends) are all at
their defaults: that voice is stock's, bit for bit. Audio callbacks never
allocate, block, perform I/O, draw or register anything; keep their cost
bounded. Stock Drumline plays above half scale, so the processor may too.

## Step locks

Hold a step and turn a page-2 encoder: the step locks that parameter for the
key the encoders edit, one choice a detent from what the step plays. Each hit
of that key on the step plays it; other keys on the step play their own.
Turning it back to the key's own choice removes the lock. While the step is held
the page shows what the step plays. Page 1 and Shift lock as stock does. The
locks live in the SDK's [step-lock handles](abi-1.2.1.md#step-lock-handles)
apart from a kit's, so a pattern copied between a kit and Drumline locks nothing
on the other.

## Projects and presets

The settings live in each pad's record, behind an owner tag made from the
extension's ID (or its `state_id`). A pad without that tag, from before or from another extension,
plays every default. A project saved with them opens on other firmware as a
stock Drumline project, the settings ignored. Stock's Drumline presets don't
keep them.

## Check and ship it

Check it with `python ../open-xy/sdk.py test --extension <folder>`, and build it
into your own firmware with `--extension <folder>`, as any
[extension](extensions.md#add-an-extension). Its tests exercise the processor on
the host. Catalog it in `catalog/extensions.json` like any other extension.

The firmware side, the sites and how storage, carrying and locks work, is in
[Drumline's second page](abi-1.2.1.md#drumlines-second-page).
