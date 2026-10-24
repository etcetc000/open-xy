# OP-XY SDK

Write an instrument in C++, audition it in a browser, and build it into
firmware for an OP-XY running OS 1.2.0. The same sound and display code
compiles to WebAssembly for previews and to Blackfin for the device. Four
examples are included: [2op FM](machines/fm2op/README.md) and
[Macro Osc](machines/macroosc/README.md), a port of Mutable Instruments
Braids, are synths; [Ring Mod](machines/ringmod/README.md) is a send effect and
[Timestretch](machines/timestretch/README.md) a sample engine.

[Open-XY](docs/builder.md), the browser builder at open-xy.com, lets OP-XY
owners pick from these machines and download firmware made from their own stock
`os.ldr`, without installing anything.

Firmware builds also keep a live-note ghost pattern for the selected instrument
track. Double-tap Record to insert the performance at its original loop
positions; **captured** confirms insertion. Stop preserves a partial take, while
starting playback again clears it. Changing tracks, patterns, projects, track
length, scale or quantization clears the ghost pattern. Notes survive loop wraps
and silence; later playing replaces nearby same-pitch notes as in stock recording.
It holds up to 120 notes, with stock collision and pattern-capacity limits. The
first Record tap snapshots the take; the second confirms capture.

For repeated live takes, set **COM > M1 > system > recording > replace**, then
lock live recording with **Record + Play**. Old notes are removed as the
playhead passes, including where you play silence. New notes survive their
current pass; held notes stay protected until released. Stop ends the take.
The default is **overdub**, restored on reboot. Momentary Record holds retain
stock behavior, and double-tapping Record still captures.

This is an unofficial, experimental SDK. A build statically links a curated
collection of machines into exact stock OS 1.2.0; arbitrary engine installation
and dynamic loading are not implemented. Native code is not sandboxed, and
build checks cannot guarantee recovery from a faulty image. Browser previews
work on any OS; firmware builds need Windows x86_64 or Apple-silicon macOS.

## Try an example

Commands say `python`; on macOS use `python3`. Requires Python 3.11+,
Node 22.12+, NumPy 2.3.5, Clang++ with wasm32/wasm-ld, and a recent Chrome or
Edge. Firmware and Blackfin tools are not needed.
Follow [Check your tools](docs/first-machine.md#check-your-tools) to set up an
isolated Python environment and verify WebAssembly support, then run:

```sh
python sdk.py preview --engine fm2op --out local/preview-001
python tools/serve_harness.py --preview local/preview-001
```

Open http://127.0.0.1:8917/ and click **Enable audio**. Use `--engine macroosc`
for the other synth. The [preview guide](docs/preview.md) covers iteration,
patches and automation.

## Write a machine

Start with [Your first machine](docs/first-machine.md): check the prerequisites,
create a custom C++ machine, play it in the browser and make an audible edit.

A machine is a folder of source: `machine.json`, which declares its kind and
stock identity, controls in `definition.json`, and C++ for its kind: a synth's
sound (`engine.cpp`) and display (`display.cpp` and artwork), a sample engine's
`sample.cpp` or an effect's `fx.cpp`. The SDK supplies adapters, storage,
previews, firmware builds and packaging.

- [Machine API](docs/api.md): platform rules, schemas, callbacks for each kind, buffers and ownership.
- [Firmware extensions](docs/extensions.md): shared controller events, callback order and exclusive commands.
- [Display API](docs/display.md): retained drawing frames and asset constraints.
- [Build your own firmware](docs/build.md): toolchain, stock files, adding a machine, build and install.
- [Open-XY builder](docs/builder.md): using the builder, web development and kit export.
- [Contributing](CONTRIBUTING.md): code ownership and checks.

## Repository layout

| Path | Owns |
| --- | --- |
| `machines/<id>/` | Machine source, definition, `machine.json` and artwork |
| `extensions/<id>/` | Firmware behavior extensions and their private helpers |
| `sdk/` | C++ interfaces, adapter templates and browser runners |
| `builder/` | The Open-XY browser builder |
| `harness/` | The local preview workbench |
| `tooling/` | The Python behind `sdk.py`: definitions, builds, firmware container checks and packaging |
| `profiles/`, `linker/` | The firmware profile, the machine collection and the memory layout |
| `tests/` | SDK contracts and infrastructure tests |
| `tools/` | Standalone scripts: the preview server, browser and firmware checkers, and compiler build and release scripts |
| `local/` | Ignored non-firmware development outputs and tool binaries |

`python sdk.py check` checks the SDK. A machine's sound tests belong to its
maintainer; adding an instrument does not add tests or Python modules to the SDK.

The SDK and most machines are MIT ([LICENSE](LICENSE), Open-XY contributors);
[Braids notices](machines/macroosc/vendor/LICENSE) apply to that port.
Timestretch, including its Capicola-derived analysis, is AGPL-3.0; see its
[source and license notices](machines/timestretch/vendor/README.md). Shared
SDK code carries no copyleft code, but firmware and builder kits that include
Timestretch, as the default collection does, are distributed under the
AGPL-3.0. Stock firmware and stock artwork are never included; they come from
the user's own device. The [toolchain](docs/toolchain.md) release ships its
GPL sources.
