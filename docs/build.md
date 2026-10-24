# Build your own firmware

`sdk.py build` statically links every machine in the collection into exact
stock OS 1.2.0, using the [firmware ABI](abi-1.2.0.md), and checks the result.
It runs on Windows x86_64 and Apple-silicon macOS. To choose among the curated
machines without building, use the [browser builder](builder.md) instead. A
machine reaches the firmware by joining the collection; read the
[caveats](../README.md) before installing.

Commands use `python`; on macOS it is usually `python3`. Building needs only
Python 3.11+, no virtual environment or packages; the browser preview in
[your first machine](first-machine.md) has its own prerequisites.

## 1. Install the tools

```sh
python sdk.py toolchain fetch
```

It downloads the pinned [toolchain](toolchain.md) for this computer, checks it
and installs it in `local/toolchain`, which every native command uses when
`--toolchain` and `--compiler` are omitted. If you downloaded the zip from the
`toolchain-1` release yourself, install it with
`python sdk.py toolchain fetch --archive <zip>`.

## 2. Copy the stock files

On an OP-XY running OS 1.2.0, hold COM while switching it on to open te-boot.
It must show "3. dev menu"; if not, enable it with the
[builder's dev menu helper](builder.md#dev-menu). Then:

1. Press track 3 for "3. dev menu"; the next screen is the factory menu.
2. Press track 3 for "3. mtp file transfer (windows)", or track 2 for
   "2. mtp file transfer (mac)", and wait for "mtp mode active". Open the
   device in File Explorer, or in OpenMTP on a Mac (quit Field Kit first).
3. Copy these files into one folder outside every checkout:

```text
stock-1.2.0/
  os.ldr               from /upgrade/firmware/blackfin/bin/os.ldr
  content/
    op1_factory.json   from /content/op1_factory.json
```

4. Press track 2 for "2. exit".

For example, keep the folder in `%LOCALAPPDATA%/OPXY-SDK/private/` on Windows or
`~/Library/Application Support/OPXY-SDK/private/` on macOS. The build accepts
only the exact stock files: an encrypted `.tfw` is a different format, and an
installed Open-XY build replaces `/content/op1_factory.json`. `--stock-content`
is whichever folder holds `op1_factory.json`.

## 3. Add your machine

Start from [your first machine](first-machine.md); `sdk.py new` needs no
preview tools. Its `machine.json` holds an engine ID and page no machine in
this repository uses. To build it for the device:

1. Move its folder to `machines/<id>/` and rename every `local_` prefix in
   `panel.svg.fragment` to `<id>_`.
2. Keep its `machine.json`, or give it another free
   [identity](api.md#identity) ([fields](api.md#registration-boundary)). Names
   and IDs are saved in presets and projects, so they never change once
   published.
3. Add `<id>` to `profiles/collection.json`. All selected machines share
   2040 KiB ([memory and limits](api.md#memory-and-limits)); `compile` shows
   each machine's share. Remove machines you don't want to make room.

`python sdk.py compile --out <folder>` then builds the module alone, without
stock files, and prints each machine's memory use: a quick check before the
full build. Like build outputs, keep `<folder>` outside the checkout.

## 4. Build

```sh
python sdk.py build --stock-os <stock-1.2.0>/os.ldr --stock-content <stock-1.2.0>/content --out <builds>/mine-001
```

Use a new `--out` folder for every build, also outside the checkout; a failed
build leaves none behind. The build fails on any check below; on success it
prints its results as JSON, then the files to install from `<out>/candidate/`.

## 5. Install

| File under `candidate/` | Device path |
| --- | --- |
| `os.ldr` | `/os.ldr` |
| `content/op1_factory.json` | `/content/op1_factory.json` |
| `content/display/<machine>.svg`, one per synth | `/content/display/<machine>.svg` |

1. Open file transfer as in step 2 and copy the files to the paths above,
   replacing existing files. Keep the device's other content.
2. Press track 2 for "2. exit".
3. Press track 5 for "5. write os to boot partition", keeping the OP-XY on power.
   When "firmware written ok!" appears, switch it off and on.

The firmware writer reads `/os.ldr`, not the upgrade directory. The browser
builder's packages also add an init preset for each instrument
(`content/presets/<machine>/default.json`); command-line packages do not.
`module.elf` and `module.bin` are development artifacts, not installable
images. The SDK's commands never write to a device. To select machines and
install through the browser builder's guide instead, export a
[personal kit](builder.md#export-a-kit) of your collection.

## What a build checks

| Check | Responsibility |
| --- | --- |
| Stock/tool hashes, exact edit intervals, entry/loader preservation, protected ranges | Shared platform/packager |
| Linked memory bounds, relocations, audio-callback recursion and stack depth, and complete output reconstruction | Shared infrastructure; the selected machines share one module limit |
| Callback layout, registration/name ownership, artwork format | SDK adapters and integration checks |
| Audio arithmetic, sound references, animation sequences and port fidelity | Selected engine's checks |

Host checks do not model device scheduling, interrupts, caches, stock graphics
or recovery, and do not guarantee a recoverable device.

## Release verification

Published packages and browser kits come from the release host named in
[`profiles/toolchains.json`](../profiles/toolchains.json), whose compiler
reproduces the reference bytes ([hosts](toolchain.md#hosts)). There, the
maintainer runs:

```sh
python sdk.py check --native-package <builds>/mine-001 --stock-os <stock-1.2.0>/os.ldr --stock-content <stock-1.2.0>/content
```

It independently rebuilds the package (`sdk.py verify`) and runs the fixture
tests that need the package and stock files, under normal Python and
`python -O`; missing inputs or skipped tests fail. The sample-service fixtures
need a host C++11 compiler (`--host-cxx`, default `g++`) on PATH. A changed
source requires a new package, not a manually changed manifest. Public `check`
ignores inherited fixture variables. A personal build doesn't need this step.
