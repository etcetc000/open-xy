# Contributing

Start with [Your first machine](docs/first-machine.md), then the
[engine API](docs/api.md) and either bundled example. Keep changes
in the layer that owns the behavior:

| Change | Location |
| --- | --- |
| Oscillator, controls, artwork, presets or lookup tables | The machine's folder |
| Sound tests | The machine maintainer's own project |
| Controller behavior, capture history and extension gestures | The extension's folder; see [extensions](docs/extensions.md) |
| Callback contract, adapter, build, preview or packaging | SDK |
| Firmware address map, shared memory reservation or compiler profile | Shared platform integration |

A machine folder holds C++ source, `definition.json` (controls, the same
schema for every kind), `machine.json` and artwork. It never holds build scripts; a new
machine is not a reason to add code to `tooling/`. A new callback contract or
native identity still needs SDK work.

## Checks

Keep firmware, stock content, extracted firmware and firmware-containing build
outputs **outside every checkout**, including ignored `local/`. The docs use
`%LOCALAPPDATA%/OPXY-SDK/private/` (Windows) or
`~/Library/Application Support/OPXY-SDK/private/` (macOS); pass those external
paths to build commands. `local/` is only for non-firmware development
artifacts. Do not force-add private inputs or generated firmware packages.

`python tools/check_private_files.py --staged` checks the actual index before
a commit. `--history --all-objects` audits all available refs, reflogs and
unreachable objects. CI also checks the current tree and fetched history.
These checks reject private paths and firmware/archive/executable signatures
even when a file has been renamed. They are additional protection, not
permission to put private files in the checkout.

```sh
python sdk.py check
```

SDK checks cover declared interfaces, storage, generated bindings and packaging.
Sound correctness is the machine maintainer's responsibility: keep sound tests
with the machine's own project, not in this repository. Native
changes also require the complete [release verification](docs/build.md#release-verification); its
required checks and private fixtures cannot be skipped.

In a PR, explain the behavior, how to try it, compatibility changes and checks
run. For a device observation, include the exact image/asset hashes and what you
actually exercised. Build success is not a hardware timing measurement.

## Documentation

Document interfaces, invariants and commands once, at their owning layer. Link
to source examples instead of narrating each implementation. Keep research
chronology, abandoned approaches, generated reports and empty qualification
forms out of the public SDK. Git history retains implementation history.
