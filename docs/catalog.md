# Catalog

Machines published from their own repositories reach the Open-XY builder
through [`catalog/machines.json`](../catalog/machines.json). The catalog pins
one tagged commit of each; nothing is copied into this repository. Each
machine's ID is held for good, so no two machines ever share one. Engine,
effect and page numbers are not the catalog's: each firmware's
[layout](api.md#identity) gives them. Authors follow the
[submission workflow below](#submit-a-machine).

## Entries

```json
{"id": "mymachine", "kind": "synth", "maintainer": "your-github-name", "status": "reserved"}
```

| Field | Contract |
| --- | --- |
| `id`, `kind` | As in the machine's `definition.json` and `machine.json` ([identity rules](api.md#identity)) |
| `maintainer` | The GitHub user who maintains it |
| `status` | `reserved`: the ID is held for a machine being written. `draft`: its release is pinned for review but not built into kits or firmware. `published`: built from its release. `retired`: no longer built; its ID stays held |
| `repository`, `tag`, `commit`, `path` | A draft or published machine's release: `https://github.com/<owner>/<repo>`, the tag, its full commit SHA, and the machine's folder in the repository (`.` at the root). A retired entry may keep them |
| `keywords` | One to five words from the [keyword list](#keywords) that people can filter the builder's machine list by. A new or updated draft or published machine lists at least one; an entry from before keywords keeps none until it changes. Machines only, not extensions |

### Keywords

`keywords` describe a machine for browsing; they are not its release `tag`. They
come from one list, `KEYWORDS` in [`tooling/catalog.py`](../tooling/catalog.py),
which `builder/core.mjs` repeats (a test compares them):

| Keyword | A machine that |
| --- | --- |
| `additive` | builds its sound from summed partials |
| `ambient` | suits slow, atmospheric textures |
| `analog` | sounds like analogue subtractive synthesis: detuned oscillators, filters, drift |
| `bass` | is made for bass lines |
| `bell` | rings with bell, chime or metallic tones |
| `chord` | plays or stacks chords from one key |
| `delay` | repeats the sound in time: echo, ping-pong, tape delay |
| `drone` | sustains or carries notes across others |
| `emulation` | recreates a specific instrument, module or effect, or is openly modelled after one ("after X") |
| `experimental` | follows an unusual model or idea more than a familiar instrument |
| `fm` | uses frequency or phase modulation between oscillators |
| `granular` | plays grains of sound |
| `lofi` | adds deliberate grit: tape wear, low bit depth, noise |
| `modulation` | is chorus, phaser, ring modulation or another modulation effect |
| `noise` | uses noise as a main source |
| `pad` | suits long, evolving pads |
| `percussion` | is struck or made for drums and hits |
| `physical` | models a physical object: strings, plates, resonators |
| `pluck` | plays short, plucked notes |
| `reverb` | adds space: spring, plate, room or diffusion |
| `vocal` | uses formants, vowels or voices |

Keywords are the catalog maintainer's call, as names are: a pull request can
propose them, and review decides. A change to keywords alone is not an update:
`catalog check --base` asks no new `cpu` record for it, and
`catalog test --changed-from` does not test it again, so review is its only check.
That also holds for keywords changed on someone else's entry.

To propose a word the list lacks (such as `filter`, `distortion` or `stereo`),
open a pull request against `staging` that adds it to both lists and to the table
above, and to the entries of the machines it fits. A word is added only when it
describes several machines and no listed word already does.

Note players carry no audio-render CPU record; their note transforms are not
covered by the profiler.

New or changed draft/published instrument/effect entries require a `cpu` record generated
from [hardware profiling](profiling.md), for synth, sample and effect machines; a synth's is a
control sweep that records its heaviest setting. Results above the recommended
3.5% of the block budget are accepted with a warning. That guide owns
the field schema, workloads and contributor commands. Unchanged legacy entries
may remain without a record until their next update; measurements are never
backfilled with host estimates.

IDs are unique across the catalog. Open-XY's own machines are entries like
any other, published from the [core machines](https://github.com/etcetc000/open-xy-core-machines).

[Firmware extensions](extensions.md) have a catalog of their own,
`catalog/extensions.json`, in the same format under `"extensions"`: `kind` is
`extension`, `path` is the extension's folder, and there is no `cpu` record
(extensions run on the controller path, not in audio). Its IDs are unique among
extensions. Submit one the same way as a machine: a pull request that changes only
that file. `sdk.py catalog check`, `fetch` and `test` cover both catalogs, and
`fetch` writes both into the collection.

The builder's machine details panel links to the catalog's maintainer profile,
repository and source at the pinned release commit. Community machines also
show a short notice about support and possible instability or unexpected audio.

## Commands

| Command | Does |
| --- | --- |
| `python sdk.py catalog check [--base <ref>] [--allow-control-changes]` | Validates the catalog. With `--base`, also against an earlier catalog: every entry stays, with its kind, and its status only moves forward, except published back to draft; a published machine whose commit moved is fetched at both commits and its [controls compared](#controls) |
| `python sdk.py catalog fetch [--tags] [--prune]` | Fetches every published machine at exactly its commit into `local/catalog/`, checks it matches its entry and writes `local/catalog/collection.json`: every published machine, in catalog order. `--tags` also confirms each tag still names its commit; `--prune` then deletes fetched folders the catalog no longer pins |
| `python sdk.py catalog test [--tags] [--changed-from <revision>] [--jobs <n>] [--no-cpu]` | Fetches, then runs [`sdk.py test`](first-machine.md#what-you-own) on every published machine. `--changed-from` tests only the entries that differ from the catalog at that git revision, as CI does for a pull request that changes only the catalog; `--jobs` checks several folders at once |

A fetched folder that changed is refused; delete it to fetch again. Pass the
collection to any build command:

```sh
python sdk.py compile --collection local/catalog/collection.json --out <folder>
```

## Submit a machine

1. **Develop in your own repository.** Use `sdk.py new --repository` as described
   in [Your first machine](first-machine.md); the
   [core machines](https://github.com/etcetc000/open-xy-core-machines) are the worked
   examples. A root-level machine uses catalog `path: "."`; a machine in a
   multi-machine repository uses its folder, such as `machines/myid`.
2. **Reserve the ID when you intend to publish.** Open a catalog-only PR against
   `main` (see step 6) with an unused ID and `reserved` entry as above. Your personal
   builds need no reservation. The ID is saved in presets and held for good; engine,
   effect and page numbers are assigned by each build's layout.
3. **Check and audition.** Run `sdk.py test --machine <folder>` and the native
   compile (`test --firmware` with the pinned tools). Extend the machine's own
   sound tests, keep CI green, and provide a description and redistribution
   licence, including any vendored licences. Keep within the shared memory
   budget printed by the build. Host CPU estimates guide development.
4. **Commit and measure on hardware.** Follow [hardware profiling](profiling.md)
   for the exact machine revision: build with profiling enabled, install when
   authorized, confirm reboot, collect a run ([one is enough](profiling.md#collect-a-run)), and publish the generated
   readout. Keep all firmware outside every checkout. A maintainer can run this
   for authors without an XY. A reserved entry can stay reserved while hardware
   evidence is pending; `draft` and `published` submissions both need it.
5. **Tag the measured commit and submit it.** Push a release tag such as
   `v1.0.0`. Change the catalog entry to `draft` for review or `published` for
   inclusion, adding `repository`, `tag`, the full tagged `commit` and `path`
   from the field table, and one to five `keywords` that describe it. Add the
   generated `cpu` member from `catalog-cpu.json`.
   A new CPU record's commit must equal the tagged machine commit. If you link a
   report, keep it in an issue/comment or separate evidence location so adding
   the readout does not change the machine commit after measurement.
6. **Validate the catalog PR.** From a checkout of the SDK, run
   `python sdk.py catalog check --base origin/main`. The PR targets `main`
   and changes only your entry in `catalog/machines.json`. A machine that needs
   SDK changes not yet released targets `staging` instead (`--base origin/staging`)
   and reaches `main` with the next release. CI fetches the pinned
   source and checks the entry; maintainer review follows below.

Updates follow the same checks: tag a new machine revision, remeasure it and
update `tag`, `commit` and `cpu` together. For a change that cannot affect CPU
cost, a catalog maintainer can approve keeping the previous `cpu` record unchanged
(still naming the commit it measured) by adding the `cpu-carried` label; the
workflow runs again and passes `--allow-carried-cpu`. Preserve the published
[control contract](#controls). Removal marks an entry `retired`; its ID is never
reused, and retirement needs no new measurement.

## Pull requests

A machine submission from a fork changes only `catalog/machines.json`; CI
refuses one that changes anything else. Changes the SDK needs for a machine are
a separate pull request. A pull request that changes only the catalog files is
labelled `catalog` automatically, and loses the label if it later changes anything
else ([Catalog label](../.github/workflows/catalog-label.yml)).

The [Catalog workflow](../.github/workflows/catalog.yml) runs on every pull
request that changes `catalog/` or anything published machines build against
(the SDK, tooling, profiles or linker script). It validates the catalog, fetches each
published machine and checks it as its own repository's CI does, and compiles
the firmware module with all of them on the pinned Windows tools. People choose
which machines they install, so the catalog doesn't need to fit the module at
once: when it doesn't, the module links Open-XY's own machines, and each other
machine must fit beside them.

Maintainers merge `main` into `staging` after each catalog merge, so both
branches carry every entry. Merged catalog changes reach the site with the next
release ([`sdk.py release`](builder.md#cloudflare-deployment)): production from
`main`, beta from `staging`.

The builder shows catalog machines as **Community**, unselected at first.
Open-XY's own examples, which maintainers list by ID (`OFFICIAL` in
[`tooling/catalog.py`](../tooling/catalog.py)), are shown as Open-XY machines
instead. A catalog entry has no field to claim this, and who hosts its
repository does not decide it.

A maintainer then reviews a new machine once, in full:

- It uses only the public headers: no assembly, raw addresses or platform code.
- Its audio callbacks never allocate, block, log or do I/O.
- Its name is sensible, and its licence allows redistribution.
- A sample engine's [own samples](api.md#own-samples) are audio its maintainer
  made or may redistribute under the machine's licence, never stock OP-XY content
  or commercial or library sounds, and the samples folder's `README.md` states
  each one's provenance and licence.
- New synths follow the [control and header conventions](ux.md): five to eight
  controls use two fixed rows and whole-page switching, with repeated shared
  controls where the second page has no alternate.
- It sounds right in the browser preview from its CI.

An update moves `tag` and `commit` and is reviewed as a diff. A sample engine's
update keeps each sample an earlier release shipped and gives new audio a new
name, since projects load [its own samples](api.md#own-samples) by name. The
workflow compares the catalog with the pull request's target branch: a change of `kind`
is refused, and a removed machine becomes `retired`, never deleted.
A new `repository` or `maintainer` is a handover: it fails the check, and a
catalog maintainer approves it in a pull request of its own.

### Controls

Projects save a machine's parameter values by index in its `definition.json`
`parameters`, so an update keeps what each saved value means. Compared with
the previous release, each parameter keeps its index, `id`, `mapping` and, for
an alternate, `control`. A removed, moved or renamed parameter, or a changed
mapping (its kind, range, values or enum labels), fails the check, which names
each by index. Parameters added after the last one and new `label`, `display`
or `default` values are reported and allowed.

A catalog maintainer approves a control change by adding the
`controls-changed` label to the pull request; the workflow runs again and
passes `--allow-control-changes`.
