# Contributing

## Branch workflow

Start SDK and builder changes from the latest `staging`, the default development
branch, and open your pull request with **`staging` as its base branch**:

```sh
git fetch origin
git switch -c my-change origin/staging
```

When contributing from a fork, use `upstream/staging` after fetching the Open-XY
repository as `upstream`. Target `etcetc000/open-xy:staging` in the pull request.
Merged work is tested at beta.open-xy.com. Maintainers promote reviewed work
with a `staging` → `main` release pull request; `main` is reserved for releases
and [catalog submissions](docs/catalog.md#submit-a-machine), which maintainers
merge from `main` into `staging`. CI rejects other pull requests aimed directly at `main`.
See [Cloudflare deployment](docs/builder.md#cloudflare-deployment).

## Where changes belong

Start with [Your first machine](docs/first-machine.md), then the
[engine API](docs/api.md) and the [core machines](https://github.com/etcetc000/open-xy-core-machines). Keep changes
in the layer that owns the behavior:

| Change | Location |
| --- | --- |
| A new machine | Its own repository, created with `sdk.py new --repository`; learn from the core machines |
| Oscillator, controls, artwork, presets or lookup tables | The machine's folder |
| Sound tests | The machine's `tests/`, run by `sdk.py test` |
| Controller behavior, capture history and extension gestures | The extension's own repository; see [extensions](docs/extensions.md) |
| Callback contract, adapter, build, preview or packaging | SDK |
| Firmware address map, shared memory reservation or compiler profile | Shared platform integration |

A machine folder holds C++ source, `definition.json` (controls, the same
schema for every kind), `machine.json`, artwork and `tests/`. It never holds
build scripts; a new machine is not a reason to add code to `tooling/`. A new
callback contract or native identity still needs SDK work. New machines live
in their own repositories and reach the builder through the [catalog](docs/catalog.md)
([submitting a machine](docs/catalog.md#submit-a-machine)).
This repository holds no machines; Open-XY's own are in the
[core machines](https://github.com/etcetc000/open-xy-core-machines), published the same way.

Firmware facts are declared once. Table sizes, sentinels and relocated
addresses come from [`tooling/stock_tables.py`](tooling/stock_tables.py):
Python imports them, native templates receive them as `@NAME@` values and the
builder reads them from the profile, which `check_profile` holds to the same
declarations. Where a value must stay literal (the hash-pinned linker script, an
untemplated platform source), `tests/test_stock_tables.py` asserts it matches.
A raised limit then changes one declaration, and a copy left behind fails a check.

## Checks

All firmware comes from the user's device. Keep firmware, stock content,
extracted firmware and firmware-containing build outputs **outside every
checkout**, including ignored `local/`. The one exception is the stock
instrument catalog,
[`profiles/opxy-1.2.1/content/op1_factory.json`](profiles/opxy-1.2.1/content/op1_factory.json),
which every build extends and builds read by default. The docs use
`%LOCALAPPDATA%/OPXY-SDK/private/` (Windows),
`~/Library/Application Support/OPXY-SDK/private/` (macOS) or
`~/.local/share/OPXY-SDK/private/` (Linux); pass those external
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
in the machine's `tests/`, where `python sdk.py test --machine <folder>` compiles
and runs them with the machine's other checks. Native
changes also require the complete [release verification](docs/build.md#release-verification); its
required checks and private fixtures cannot be skipped.

Work is tracked as issues. Each release has a milestone and a matching
[project board](https://github.com/etcetc000/open-xy/projects) named after its
version; maintainers assign new issues to both. Pick an open issue or open one
before larger work, and write `Closes #<issue>` in the pull request. A release merges `staging`, or a `release/<version>` branch cut from it when staging carries later work, into `main`,
is tagged `v<milestone>` with generated release notes, and closes its milestone.

In a PR, explain the behavior, how to try it, compatibility changes and checks
run. For a device observation, include the exact image/asset hashes and what you
actually exercised. Build success is not a hardware timing measurement. New or changed catalog
submissions require the revision-bound [hardware CPU readout](docs/profiling.md).

## Documentation

Document interfaces, invariants and commands once, at their owning layer. Link
to source examples instead of narrating each implementation. Keep research
chronology, abandoned approaches, generated reports and empty qualification
forms out of the public SDK. Git history retains implementation history.
