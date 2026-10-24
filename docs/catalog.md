# Catalog

Machines published from their own repositories reach the Open-XY builder
through [`catalog/machines.json`](../catalog/machines.json). The catalog pins
one tagged commit of each; nothing is copied into this repository. It also
holds every identity reserved for a machine, so no two machines ever share an
ID, engine or effect ID or page. Authors follow the
[submission guide](https://github.com/etcetc000/open-xy-machine-template/blob/main/SUBMITTING.md).

## Entries

```json
{"id": "pluck", "kind": "synth", "identity": {"engine": 1, "page": 84},
 "maintainer": "etcetc000", "status": "published",
 "repository": "https://github.com/etcetc000/open-xy-pluck", "tag": "v1.0.0",
 "commit": "ca936813d3e98c72d5c29c4d1c37dc1d9c4018b3", "path": "."}
```

| Field | Contract |
| --- | --- |
| `id`, `kind`, `identity` | As in the machine's `definition.json` and `machine.json` ([identity rules](api.md#identity)) |
| `maintainer` | The GitHub user who maintains it |
| `status` | `reserved`: the identity is held for a machine being written. `published`: built from its release. `retired`: no longer built; its ID and identity stay held |
| `repository`, `tag`, `commit`, `path` | A published machine's release: `https://github.com/<owner>/<repo>`, the tag, its full commit SHA, and the machine's folder in the repository (`.` at the root). A retired entry may keep them |

IDs and identities are unique across the catalog and the bundled machines.
`sdk.py new` suggests identities that neither holds.

## Commands

| Command | Does |
| --- | --- |
| `python sdk.py catalog check [--base <ref>]` | Validates the catalog against itself and the bundled machines. With `--base`, also against an earlier catalog: every entry stays, with its kind and identity, and its status only moves forward |
| `python sdk.py catalog fetch [--tags]` | Fetches every published machine at exactly its commit into `local/catalog/`, checks it matches its entry and writes `local/catalog/collection.json`: the bundled machines, then every published one. `--tags` also confirms each tag still names its commit |
| `python sdk.py catalog test [--tags]` | Fetches, then runs [`sdk.py test`](first-machine.md#what-you-own) on every published machine |

A fetched folder that changed is refused; delete it to fetch again. Pass the
collection to any build command:

```sh
python sdk.py compile --collection local/catalog/collection.json --out <folder>
```

## Pull requests

The [Catalog workflow](../.github/workflows/catalog.yml) runs on every pull
request that changes `catalog/`. It validates the catalog, fetches each
published machine and checks it as its own repository's CI does, and compiles
the firmware module with the bundled machines and all published ones on the
pinned Windows tools.

A maintainer then reviews a new machine once, in full:

- It uses only the public headers: no assembly, raw addresses or platform code.
- Its audio callbacks never allocate, block, log or do I/O.
- Its name and identity are sensible, and its licence allows redistribution.
- It sounds right in the browser preview from its CI.

An update moves `tag` and `commit` and is reviewed as a diff. The workflow
compares the catalog with the pull request's target branch: a change of `kind`
or `identity` is refused, and a removed machine becomes `retired`, never deleted.
