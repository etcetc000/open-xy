# Repository instructions

## First, which task is this?

- **Making a machine** (an instrument or effect, a "plugin"): it does not go in
  this repository, which holds only the SDK and the catalog. Start with
  `python sdk.py new --repository --id <id> --name "<Name>" --out ../<id>`
  (`--kind effect` for a send effect), then follow its generated README and
  AGENTS.md. Read the relevant core machines below as the worked examples.
  Use this checkout as the SDK beside the new repository,
  for example `python ../open-xy/sdk.py test --machine .`. Do not
  add a machine to this repository, or change `sdk/`, `tooling/` or tests, to
  make a machine work. If the SDK lacks something it needs, that is a separate SDK
  pull request.
- **Submitting a machine to the builder:** a pull request against `main` that
  changes only `catalog/machines.json`, adding or updating that machine's one
  entry ([submission workflow](docs/catalog.md#submit-a-machine)). A machine that
  needs SDK changes only on `staging` targets `staging` instead. CI refuses a
  submission from a fork that changes anything else.
- **Changing how the OP-XY behaves** (an extension: recording, controls, the
  sequencer): like a machine, it lives in a repository of its own and reaches
  the builder through `catalog/extensions.json`; capture, in
  open-xy-core-machines, is the worked example. Read
  [extensions](docs/extensions.md) first, and check it with
  `python ../open-xy/sdk.py test --extension .`. Reuse or widen an existing
  event; a new event, claim or action is a separate SDK pull request, proposed
  as that page describes, never a hook beside an existing one for the same
  stage. Extensions never patch the firmware or name stock addresses.
- **Changing the SDK, builder or docs:** branch from `origin/staging` and open the
  pull request against `staging`. Apart from catalog entries, `main` only receives
  releases from `staging`; never base other work on it or target it.

This repository holds no machines. Examples to read before writing one are Open-XY's
[core machines](https://github.com/etcetc000/open-xy-core-machines): synths 2op FM, Macro Osc and Karplus, the Timestretch
sample engine, and the Ring Mod and Ping Pong send effects, each published
through the catalog like any other machine.

For machine work, follow the complete [authoring journey](docs/first-machine.md)
and the [UX conventions](docs/ux.md): implement and test, offer audition where supported, then prepare a development
build and propose an authorized hardware test. Follow [hardware profiling](docs/profiling.md)
to collect the revision-bound CPU readout required for catalog submission. A
synth's readout is a control sweep (the default for profiling builds); when it
reports a result above the recommended 3.5%, tell the user which setting is
heaviest and suggest making that path lighter before submitting.
If hardware is unavailable, say that validation remains pending; a maintainer
can measure it. Never treat a successful host check as a measured CPU result or
hardware acceptance. A sample engine uses the public sample API and Timestretch
as its worked example; `new` currently scaffolds synths and effects.

## SDK changes

Read README.md and CONTRIBUTING.md. For native changes also read
[the firmware ABI](docs/abi-1.2.1.md) and [building firmware](docs/build.md).

- Engine-specific source, metadata, generators and sound tests belong under that
  engine. Keep `tooling/` and root `tests/` focused on shared contracts.
- Contributor C++ contains no platform conditionals, OS headers or assembly.
  Put platform support in `sdk/`. Audio callbacks must not allocate, block,
  log, perform I/O, register engines or call graphics.
- Preserve exact stock/tool authentication, allowed edits, entry/loader records,
  protected ranges, reserved memory, occupied-ID guards and full reconstruction.
  Do not change expected hashes merely to admit a failed build.
- Keep tools and non-firmware generated outputs in ignored `local/`; firmware,
  stock artwork and user data live outside the checkout (below). Use fresh
  output directories and the allowlisted preview server.
- Run `python sdk.py check` for every change. Machine tests are the machine
  maintainer's responsibility and do not live in this repository.
  Native changes to this repository require the build and
  [release verification](docs/build.md#release-verification), with no skipped
  required fixtures. Checks use `require()`, never `assert` (a test enforces it).
- For mandatory catalog CPU measurements and authorized hardware tests, follow
  [hardware profiling](docs/profiling.md); host estimates cannot fill the catalog field.
- Build and verification commands are local. Device operations require a separate
  user instruction. Do not infer measured cycles or recovery from host checks.
- Copy files to a connected OP-XY with `python sdk.py device push <folder>`
  (`list` and `pull` read it), never with a new transfer script. It mirrors the
  folder onto the device root with backups, readback and rollback; see
  [install](docs/build.md#5-install). If it fails, fix `tooling/device.py`.
- Keep public docs about current interfaces and behavior. Remove obsolete text
  instead of adding another status page or repeating caveats across every guide.

User instructions take precedence over this file.

## Private firmware storage

All firmware comes from the user's device. Firmware, extracted firmware,
firmware-containing packages and stock content must live outside every
checkout, including ignored local/ directories. The one exception is the stock
instrument catalog, profiles/opxy-1.2.1/content/op1_factory.json, kept for
development and checked against the profile's hash; never add other stock
files beside it. The docs use %LOCALAPPDATA%/OPXY-SDK/private/ (Windows),
~/Library/Application Support/OPXY-SDK/private/ (macOS) or
~/.local/share/OPXY-SDK/private/ (Linux); pass external
input/output paths to build commands. Never force-add these files or bypass the commit/push guards.
