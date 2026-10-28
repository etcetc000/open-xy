# Build your own firmware

`sdk.py build` statically links every machine in the collection into exact
stock OS 1.2.1, using the [firmware ABI](abi-1.2.1.md), and checks the result.
It runs on Windows x86_64, Apple-silicon macOS and Linux x86_64. To choose among the curated
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

### On Linux

Linux x86_64 has no download yet; build the pinned tools with Docker instead,
from the checkout (it takes several minutes). The tools then run on any
x86_64 distribution with glibc 2.36 or later, such as Debian 12, Ubuntu 24.04,
Fedora 37 or Arch; Ubuntu 22.04 and musl-based distributions such as Alpine
are too old or incompatible. The `z` mount option lets the container read and
write the checkout on SELinux hosts such as Fedora, and changes nothing elsewhere.

```sh
git init local/blackfin-plus-gnu
git -C local/blackfin-plus-gnu fetch --depth 1 https://github.com/deanm1278/blackfin-plus-gnu e4408a15b9be6cd911283f103a1a3f69e9a6b8c0
git -C local/blackfin-plus-gnu checkout FETCH_HEAD
mkdir local/toolchain-build
docker build -t open-xy-toolchain-linux - < tools/gcc/linux.Dockerfile
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$PWD":/opt/opxy-toolchain/open-xy:ro,z \
  -v "$PWD/local/blackfin-plus-gnu":/opt/opxy-toolchain/fork:z \
  -v "$PWD/local/toolchain-build":/opt/opxy-toolchain/out:z \
  open-xy-toolchain-linux /opt/opxy-toolchain/open-xy/tools/gcc/build-linux.sh \
  /opt/opxy-toolchain/fork /opt/opxy-toolchain/out/blackfin
mv local/toolchain-build/blackfin local/toolchain
```

Native commands then use `local/toolchain` and check every tool against the
`linux-x86_64` pins ([how it is built](toolchain.md#native-tools)).

## 2. Copy the OS from your OP-XY

On an OP-XY running OS 1.2.1, hold COM while switching it on to open te-boot.
It must show "3. dev menu"; if not, enable it with the
[builder's dev menu helper](builder.md#dev-menu). Then:

1. Press track 3 for "3. dev menu"; the next screen is the factory menu.
2. Press track 3 for "3. mtp file transfer (windows)", or track 2 for
   "2. mtp file transfer (mac)", and wait for "mtp mode active". Open the
   device in File Explorer, or in OpenMTP on a Mac (quit Field Kit first).
   On Linux, use track 3 and open the device in your file manager; GNOME
   Files lists it as "OP XY". The file manager needs MTP support (gvfs on
   GNOME or Cinnamon, kio-extras on KDE).
3. Copy the OS into a folder outside every checkout:

```text
stock-1.2.1/
  os.ldr               from /upgrade/firmware/blackfin/bin/os.ldr
```

4. Press track 2 for "2. exit".

If the OP-XY freezes on "mtp mode active" as soon as a computer connects,
reinstall the official OS 1.2.1 update (`.tfw`) and try again.

For example, keep the folder in `%LOCALAPPDATA%/OPXY-SDK/private/` on Windows,
`~/Library/Application Support/OPXY-SDK/private/` on macOS or
`~/.local/share/OPXY-SDK/private/` on Linux. All firmware comes
from your device; the build accepts only the exact stock OS, and an encrypted
`.tfw` is a different format. The stock instrument catalog the build extends,
`op1_factory.json`, is in this repository
([`profiles/opxy-1.2.1/content/op1_factory.json`](../profiles/opxy-1.2.1/content/op1_factory.json)) and is the
default `--stock-content`; pass another folder holding the same file to use
your own copy.

## 3. Add your machine

Start from [your first machine](first-machine.md); `sdk.py new` needs no
preview tools. A machine is a folder that can live anywhere, such as its own
repository. Its `machine.json` names the SDK version it targets (`"sdk": "1.0"`)
and the engine (or effect) number and page it asks for, outside the core
machines' fixed numbers ([identity](api.md#identity),
[fields](api.md#registration-boundary)). Names are saved in presets and never
change once published.

Projects save machine numbers, which a build takes from a layout: every package
holds `layout.json`, the numbers its firmware has given out. Pass it to your next
build with `--layout <previous package>/layout.json`, and every machine keeps its
numbers, even after you add or drop others.

Every build command takes the collection:

| Option | Machines built |
| --- | --- |
| `--collection <file>` | The file's `{"path": "<folder>"}` entries, relative to the file. `sdk.py catalog fetch` writes one with every [published machine](catalog.md) |
| `--machine <folder>` | The machine in `<folder>`, after the collection's; repeatable |

A build needs at least one machine; the SDK holds none of its own.

All selected machines share 2040 KiB ([memory and limits](api.md#memory-and-limits));
`compile` shows each machine's share. To make room, list fewer machines in your
own collection file.

`python sdk.py compile --machine <folder> --out <out>` then builds the module
alone, without stock files, and prints each machine's memory use: a quick check
before the full build. Like build outputs, keep `<out>` outside the checkout.
It also prints the [CPU planning allocation](api.md#cpu) and a warning that
successful compilation does not qualify real-time performance. `sdk.py test`
provides host cost estimates for supported kinds and explicitly reports
unestimated sample-engine costs.
The SDK builds a machine whose `sdk` has its major version and an equal or
older minor version.

## 4. Build

```sh
python sdk.py build --machine <folder> --stock-os <stock-1.2.1>/os.ldr --out <builds>/mine-001
```

The package records its collection, so verifying or exporting it needs no
collection options.

SDK `build` and `compile` keep stock per-voice diagnostic profiling enabled by
default. Pass `--profiling off` to bypass its oscillator, filter and whole-voice
timing bookkeeping, matching firmware generated by the website. Use
`--profiling on` (the default) for development and hardware profiling. The
package records this choice; `verify` rebuilds the same mode.
To collect the measurements, follow [hardware CPU profiling](profiling.md).

This flag leaves the OS clocks, deadline checks, coarse timing counters and
diagnostic transport intact. It does not include private experimental probes.

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
builder's packages also add each instrument's presets
(`content/presets/<machine>/`: an init preset and the machine's
[own](api.md#presets)); command-line packages do not.
`module.elf` and `module.bin` are development artifacts, not installable
images. Build commands never write to a device; hardware profiling sends
explicit diagnostic commands but does not install firmware. To select machines and
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
python sdk.py check --native-package <builds>/mine-001 --stock-os <stock-1.2.1>/os.ldr --stock-content profiles/opxy-1.2.1/content
```

It independently rebuilds the package (`sdk.py verify`) and runs the fixture
tests that need the package and stock files; missing inputs or skipped tests
fail. The sample-service fixtures
need a host C++11 compiler (`--host-cxx`, default `g++`) on PATH. A changed
source requires a new package, not a manually changed manifest. Public `check`
ignores inherited fixture variables. A personal build doesn't need this step.
