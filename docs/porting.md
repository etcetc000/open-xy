# Port to a new OP-XY OS release

When teenage engineering ships a new OS, the SDK's profile and platform layer
must be moved to it: every reviewed edit and every stock address the SDK uses
sits somewhere else, and some code changes. This is a maintainer task; it needs
the new stock files and an OP-XY.

## 1. Collect the stock files

Update an OP-XY with TE's updater, connect it in MTP mode and copy, into the
[private folder](build.md#2-copy-the-os-from-your-op-xy) as `stock-<version>/`:

| File | From the device |
| --- | --- |
| `os.ldr` | `upgrade/firmware/blackfin/bin/os.ldr` |
| `bootloader.ldr` | `upgrade/firmware/blackfin/bin/bootloader.ldr` |
| `content/` | `upgrade/firmware/blackfin/content/` |

Not the `os.ldr` at the device's root: that is whatever the last install copied
there, often an open-xy build. Compare `bootloader.ldr` with the profile's
`bootloader_reference_sha256`; a changed bootloader needs a separate review
before any install. The update restores `content/` to stock, removing open-xy's
screens, presets and catalog entries.

## 2. Run the port report

```sh
python tools/os_port.py --source <private>/stock-<old>/os.ldr --target <private>/stock-<new>/os.ldr --out <private>/ports/<new>-001
```

The output holds excerpts of stock code: keep it in the private folder. It
contains:

| File | Contents |
| --- | --- |
| `report.txt` | Counts, then each edit and address that needs review |
| `report.json` | Every edit and address, how it was found and where |
| `address-map.json` | Each stock address the platform layer, tooling, tests and docs name, in the new release |
| `profile-candidate.json` | The profile with each placed edit's new address, offset and stock bytes; unplaced edits marked `port_status: review` |

An edit is *verified* when its bytes are identical at the matching place, or
when it replaces a call that still reaches the same stock function. *Located*
edits sit at the matching place with changed bytes: an address load whose
target moved (the relocated-table edits) or reworked code. Read each located
and review edit.

The address scan finds addresses with or without `0x`, in code, comments and
docs, and those assembly builds from half-words. An address in RAM outside the
loaded image, and one inside a structure, is mapped through the code that loads
it ("references"): confirm those against a load in the new release.

## 3. Resolve what the report leaves

Disassemble with the pinned toolchain:

```sh
<toolchain>/bin/bfin-elf-objdump -D -b binary -m bfin --adjust-vma=<address> <bytes.bin>
```

Check in particular:

- **Relocated tables** ([`tooling/stock_tables.py`](../tooling/stock_tables.py)):
  each base-load site still loads its table, and the stock counts in the bound
  sites are unchanged. A new stock engine or effect changes the identity rules.
- **The boot label** ([`tooling/startup_label.py`](../tooling/startup_label.py)):
  the stub replays a stack load whose offset can change, then calls the stock
  renderer and resumes after the site.
- **Hooks that replace an instruction**: the replaced instruction can use
  another register, or move next to a different one, in the new release (a
  load stub returns its result in the register stock reads next; see
  `HOOK_RESULT_R3` in `hook.inc`).
- **Hooks that replay displaced instructions**, such as the profiling bypasses
  in `profiling.s`: compare the code between each site and its continuation.
- **Generated code**: [`tooling/adapters.py`](../tooling/adapters.py) and other
  generators emit stock calls; the scan covers all of `tooling/`.
- **Protected ranges** in [`tooling/pipeline.py`](../tooling/pipeline.py).
- **Stock object layouts** the platform layer reads (the screen object, SVG
  path and text leaves, records): sizes and field offsets are not checked by
  the report.
- **The stock benchmark table** used by profiling: the same engines in the
  same order.

## 4. Move the SDK

One OS is supported at a time. Rename the profile, its content folder, the
platform folder, the linker script and the firmware ABI page to the new
version, apply the address map and your resolutions, and set the profile's
identity, hashes and layout. Then:

- Add the old profile's ID to `layout_profiles` when the identity rules are
  unchanged, so layouts from the old release keep their machines' numbers.
- Empty `tests/fixtures/released_profiles.json` and set its stock hash: builds
  of the old OS cannot be recovered, and TE's updater replaces them.
- Update the builder's version text and its `.tfw` link, and the tests that pin
  stock addresses or bytes.

Run `python sdk.py check` and [release verification](build.md#release-verification).

## 5. Build and test on hardware

Build a profiling-enabled package for the new release and verify it
(`sdk.py verify`). Clear `__pycache__` folders after regenerating sources: a
rewritten file with an unchanged size and timestamp can keep a stale cache.
With the user's go-ahead, install it and walk the hardware list: boot label;
stock engines, effects, sequencer, projects and MTP; each custom synth's screen,
sound, controls and fine turns; page switching; LFO destinations; custom send
effects, including one with a delay buffer; several machines and stock engines
across tracks; project companions after a power cycle. Photograph any exception
screen: its fault address, `RETX` and call stack point at the code.

## 6. Compare CPU

Run the [benchmark](profiling.md) on each release (one run each is enough;
more give a median) and compare:

```sh
python tools/cpu_compare.py --a <private>/<old-build> <old-run-001> \
                            --b <private>/<new-build> <new-run-001> --all
```

The stock engines run in every profiling build, so the stock stages compare
one release's code with the other's.
