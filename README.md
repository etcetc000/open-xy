# OP-XY SDK

Write an instrument in C++, audition it in a browser, and build it into
firmware for an OP-XY running OS 1.2.1. The same sound and display code
compiles to WebAssembly for previews and to Blackfin for the device.

This repository is the SDK and the [catalog](docs/catalog.md) of published
machines; it holds no machines itself. Every machine lives in a repository of
its own. Open-XY's own machines are in
[open-xy-core-machines](https://github.com/etcetc000/open-xy-core-machines): synths 2op FM, Macro Osc (a port of Mutable
Instruments Braids) and Karplus, the Timestretch sample engine, and the Ring
Mod and Ping Pong send effects. Read them as examples.

[Open-XY](docs/builder.md), the browser builder at open-xy.com, lets OP-XY
owners pick from the catalog's machines and download firmware made from their
own stock `os.ldr`, without installing anything.

**Contributing? Start feature branches from `staging` and open pull requests
against `staging`.** It is the development branch; `main` receives reviewed
releases from `staging` or a `release/<version>` branch cut from it, and machine submissions (catalog-only PRs) go to `main`. See the [contributor workflow](CONTRIBUTING.md#branch-workflow).

With Retrospective capture enabled, firmware keeps a live-note ghost pattern for
the selected instrument track. Double-tap Record to insert the performance at
its original loop positions; **captured** confirms insertion. Stop preserves a partial take, while
starting playback again clears it. Changing tracks, patterns, projects, track
length, scale or quantization clears the ghost pattern. Notes survive loop wraps
and silence; later playing replaces nearby same-pitch notes as in stock recording.
It holds up to 120 notes, with stock collision and pattern-capacity limits. The
first Record tap snapshots the take; the second confirms capture.

This is an unofficial, experimental SDK. A build statically links a curated
collection of machines into exact stock OS 1.2.1; arbitrary engine installation
and dynamic loading are not implemented. Native code is not sandboxed, and
build checks cannot guarantee recovery from a faulty image. Browser previews
work on any OS; firmware builds need Windows x86_64, Apple-silicon macOS or
Linux x86_64.

## Try an example

Commands say `python`; on macOS use `python3`. Requires Python 3.11+,
Node 22.12+, NumPy 2.3.5, Clang++ with wasm32/wasm-ld, and a recent Chrome or
Edge. Firmware and Blackfin tools are not needed.
Follow [Check your tools](docs/first-machine.md#check-your-tools) to set up an
isolated Python environment and verify WebAssembly support. Clone the core
machines next to this repository, then run:

```sh
git clone https://github.com/etcetc000/open-xy-core-machines ../open-xy-core-machines
python sdk.py preview --machine ../open-xy-core-machines/machines/fm2op --out local/preview-001
python tools/serve_harness.py --preview local/preview-001
```

Open http://127.0.0.1:8917/ and play a key or click **Play phrase**; audio starts automatically. Use
`machines/macroosc` or `machines/karplus` for another synth. The
[preview guide](docs/preview.md) covers iteration, patches and automation.

## Write a machine

Start with [Your first machine](docs/first-machine.md): check the prerequisites,
create a custom C++ machine, play it in the browser and make an audible edit.
The [core machines](https://github.com/etcetc000/open-xy-core-machines) are the
worked examples. Create a repository-ready starter directly from the SDK:

```sh
python sdk.py new --repository --id mymachine --name "My Machine" --out ../mymachine
```

Synths start with four controls and the stock header. Choose `--controls 5`,
`6`, `7` or `8` when the sound needs more controls; these use the shared
two-page header. Unused second-page columns repeat the same control from page 1.
It includes source, tests, README, agent instructions, licence and CI. Use
`--kind effect` for a send effect; omit `--repository` when adding just a machine
folder to an existing repository. Follow the generated instructions through
audition, [hardware profiling](docs/profiling.md) and
[catalog submission](docs/catalog.md#submit-a-machine).

A machine is a folder of source: `machine.json`, which declares its kind (each
build gives its stock numbers), controls in `definition.json`, and C++ for its kind: a synth's
sound (`engine.cpp`) and display (`display.cpp` and artwork), a sample engine's
`sample.cpp` or an effect's `fx.cpp`. The SDK supplies adapters, storage,
previews, firmware builds and packaging.

- [Machine API](docs/api.md): platform rules, schemas, callbacks for each kind, buffers and ownership.
- [Firmware extensions](docs/extensions.md): shared controller events, callback order and exclusive commands.
- [Display API](docs/display.md): retained drawing frames and asset constraints.
- [Machine UX](docs/ux.md): the conventions machines share for controls, pages, screens and motion.
- [Build your own firmware](docs/build.md): toolchain, stock files, adding a machine, build and install.
- [Open-XY builder](docs/builder.md): using the builder, web development and kit export.
- [Hardware CPU profiling](docs/profiling.md): measure your machine and publish the required catalog readout.
- [Port to a new OS release](docs/porting.md): move the profile and platform layer to a new stock OP-XY OS, test it and compare CPU.
- [Catalog](docs/catalog.md): machines published from their own repositories, pinned by commit.
- [Project companions](docs/projects.md): the `.oxy` file recording which machine each project number meant.
- [Contributing](CONTRIBUTING.md): code ownership and checks.

## Repository layout

| Path | Owns |
| --- | --- |
| `sdk/` | C++ interfaces, adapter templates and browser runners |
| `builder/` | The Open-XY browser builder |
| `harness/` | The local preview workbench |
| `tooling/` | The Python behind `sdk.py`: definitions, builds, firmware container checks and packaging |
| `profiles/`, `linker/` | The firmware profile and the memory layout |
| `catalog/` | The machines the builder ships, each pinned to a commit of its own repository ([catalog](docs/catalog.md)) |
| `tests/` | SDK contracts and infrastructure tests |
| `tools/` | Standalone scripts: the preview server, browser and firmware checkers, and compiler build and release scripts |
| `local/` | Ignored non-firmware development outputs and tool binaries |

`python sdk.py check` checks the SDK; `python sdk.py test --machine <folder>`
checks a machine, including the sound tests in its own `tests/`. Adding an
instrument does not add tests or Python modules to the SDK.

The SDK is MIT ([LICENSE](LICENSE), Open-XY contributors). Each machine
carries its own licence in its repository. Timestretch, in the
[core machines](https://github.com/etcetc000/open-xy-core-machines), includes Capicola-derived analysis under the
AGPL-3.0; the SDK carries no copyleft code, but firmware and builder kits that
include Timestretch are distributed under the AGPL-3.0. No firmware is included: all firmware comes from the user's own
device. The one stock file in this repository is the instrument catalog every
build extends, [`op1_factory.json`](profiles/opxy-1.2.1/content/op1_factory.json),
kept for development; the builder distributes it too. The
[toolchain](docs/toolchain.md) release ships its GPL sources.
