# Local preview and patch protocol

Browser previews audition synths: their controls, alternates included, their
sound and their own screen.
Sample engines with an authored drawing display have a silent, interactive
screen preview: controls and held keys drive the same display callback and SVG
renderer used by synths. It has no sample loading, waveform data or audio;
sample playback and browser audition run on the OP-XY. Stock sample pages and
effects have no browser preview. Machine-owned host tests and native builds
check their audio callbacks.

For a new instrument, follow [Your first machine](first-machine.md). Check the
preview toolchain with `python sdk.py doctor --preview`; custom schema-1 sources
build with `python sdk.py preview --source /path/to/machine --out local/custom-001`.
Custom sources always use local authoring checks and cannot qualify a collection.

Build a machine folder, such as one of the [core machines](https://github.com/etcetc000/open-xy-core-machines) cloned
beside this repository, then serve its authenticated snapshot:

```sh
python sdk.py preview --machine ../open-xy-core-machines/machines/fm2op --out local/preview-001
python tools/serve_harness.py --preview local/preview-001
```

Use `--machine /path/to/machine` for an external synth or sample drawing page.
Silent previews label the audio control **Display only**; play keys without
enabling audio to exercise the display's note activity.

Use `--cxx /path/to/clang++` if Clang is not on PATH, or `--port 8937` if the
server port is occupied. Open http://127.0.0.1:8917/, enable audio, then play
`A W S E D F T G Y H U J K` or the on-screen keyboard. Escape/Stop all and loss
of window focus release notes. Monitor gain is separate from engine parameters.

To change sound or display, edit the C++, rebuild into a fresh directory,
restart the server and reload. Edit `definition.json` for controls, then
rebuild.
The server rejects stale sources at startup; an already running server keeps
its selected snapshot. It serves a file allowlist, never the repository root.

Every preview build runs shared ABI, fixed-memory and drawing smoke checks.
Audio previews also check input ownership, declared peak and voice lifecycle. They do not prove
callback correctness or timing; sound correctness is the machine maintainer's
responsibility.

## Host behavior

The AudioWorklet runs compiled C++ at 44.1 kHz in 64-sample blocks, with eight
preallocated voices, a 5 ms attack and 120 ms release audition gate. Controls
apply at the next kernel block; retrigger discards pending samples immediately.
The preview does not emulate stock envelopes, filters, LFOs or effects, and
cannot measure Blackfin CPU use. No firmware or device connection is required.

## Patch format

[controls.mjs](../sdk/browser/controls.mjs) implements validation.
A browser patch contains:

| Field | Value |
| --- | --- |
| `kind` | `opxy-machine-preview` |
| `machine` | Engine ID |
| `parameters` | Exactly the declared normalized controls, finite and in [0, 1] |
| `view` | `routing` (the machine's screen) or `waveform` (an audio scope) |
| `parametersQ31` | Eight words: ordered rounded Q31 controls followed by zeros |

Import validates the Q31 array against the normalized values. Invalid input leaves the current patch unchanged. The UI
limits patch files to 16 KiB. Exported JSON is a browser patch, not an OP-XY
project; exported SVG is a single frame, not executable display code.

## Local automation

After enabling audio through the UI, `window.machineLab` provides:

| Method | Input/result |
| --- | --- |
| `getPatch()` / `loadPatch(patch)` | Export/import the same validated format |
| `setParameters(values)` | Complete map of normalized controls |
| `noteOn(midi, velocity = 0.8)` / `noteOff(midi)` | MIDI note 0..127; velocity (0, 1] |
| `panic()` | Stop all voices |
| `getStatus()` | Audio state, backend identity, note counts and output RMS |

The SDK-owned AudioWorklet transport accepts `{type: 'parameters', value}`
(Q31 array), `{type: 'on', note, velocity}`, `{type: 'off', note}` and
`{type: 'panic'}`. It reports readiness/layout or an error. Contributors use the
C++ callbacks; they do not implement this transport themselves.

Preview modules expose ABI version 1, compiled state/buffer sizes, static voice
addresses and lifecycle/render exports. The runner rejects imports, invalid
layout and a sample-rate mismatch. The manifest binds the WASM, artwork and
source hashes and carries the definition and `machine.json` description that
the workbench page shows. These are host interfaces, not a device upload protocol.

## Checks

Run `python sdk.py check`. For a real-browser check, start a dedicated Chrome with a local debugging port and run
`node tools/check_harness_browser.mjs`. `OPXY_CHROME_PORT` and
`OPXY_PREVIEW_PORT` select its endpoints. It exercises audio, controls, patch
round trips and layout. Its expectations come from the served definition, so it
runs against any preview; screenshots remain in ignored `local/`.
