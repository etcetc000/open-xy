# Build your own firmware

`sdk.py build` statically links every machine in the collection into exact
stock OS 1.2.1, using the [firmware ABI](abi-1.2.1.md), and checks the result.
It runs on Windows x86_64, Apple-silicon macOS and Linux x86_64. To choose among the curated
machines without building, use the [browser builder](builder.md) instead. A
machine reaches the firmware by joining the collection; read the
[caveats](../README.md) before installing.

Commands use `python`; on macOS it is usually `python3`. Building needs only
Python 3.11+, no virtual environment or packages (copying to the device with `device push` also needs comtypes on Windows or the libusb library on macOS and Linux, see [install](#5-install)); the browser preview in
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

If MTP fails to connect or freezes, follow the builder’s
[MTP troubleshooting guide](builder.md#mtp-troubleshooting-on-macos). It offers a fresh OS 1.2.1 setup and separates missing-OS recovery from
a drive format that erases user data.

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
repository. Its `machine.json` names the SDK version it targets (`"sdk": "1.5"`)
and, for a sample engine, the stock sampler it forks; the build gives its
numbers ([identity](api.md#identity), [fields](api.md#registration-boundary)).
Names are saved in presets and never change once published.

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

All selected machines share 2032 KiB ([memory and limits](api.md#memory-and-limits));
`compile` shows each machine's share. To make room, list fewer machines in your
own collection file.

`python sdk.py compile --machine <folder> --out <out>` then builds the module
alone, without stock files, and prints each machine's memory use: a quick check
before the full build. Like build outputs, keep `<out>` outside the checkout.
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
| `content/presets/<machine>/`, one per instrument (not effect): an init preset and the machine's [own](api.md#presets) | `/content/presets/<machine>/` |
| `content/samples/<machine>/`, for a sample engine shipping [its own samples](api.md#own-samples) | `/content/samples/<machine>/` |

1. Open file transfer as in step 2 and copy the files to the paths above,
   replacing existing files. Keep the device's other content.
   `python sdk.py device push <out>/candidate` does this instead:
   it backs up each file it replaces, reads
   every copied file back and restores the backups if anything fails, keeping
   them and a record under the private folder. It never writes the boot partition.
   On Windows it needs `python -m pip install comtypes==1.4.17`. On macOS and Linux it
   speaks MTP over libusb, which needs only the library (`brew install libusb`, or
   `apt install libusb-1.0-0`), the device in "mtp file transfer (mac)" mode and no
   other app holding it (Field Kit, OpenMTP, Android File Transfer, the browser
   builder); on Linux, a udev rule may be needed for access to the USB device.
   To copy without backups or device readback, pass `--no-backup --no-verify`
   together. This replaces every supplied file and records each write, but
   cannot restore earlier files if interrupted.
2. Press track 2 for "2. exit".
3. Press track 5 for "5. write os to boot partition", keeping the OP-XY on power.
   When "firmware written ok!" appears, switch it off and on.

The firmware writer reads `/os.ldr`, not the upgrade directory. The browser
builder's packages also add each instrument's presets
(`content/presets/<machine>/`: an init preset and the machine's
[own](api.md#presets)).
`module.elf` and `module.bin` are development artifacts, not installable
images. Build commands never write to a device; hardware profiling sends
explicit diagnostic commands but does not install firmware. To select machines and
install through the browser builder's guide instead, export a
[personal kit](builder.md#export-a-kit) of your collection.

### Collection-file inspection prototype

`build --collection-probe` keeps machines statically linked and adds a startup
file inspector. It **does not load or execute external machines**. It reads
`/projects/openxy/collection.oxc` once per processor role, validates its format,
platform fingerprint, sizes and SHA-256, and writes `gui-status.oxc` and
`audio-status.oxc` in the same folder. These are JSON reports with
`"execution": false`; a successful report on one role establishes nothing about
the other. Missing, incompatible, truncated or corrupt files do not register
machines or change callback destinations. No executable RAM or cache settings
are changed by the inspector. Native file calls use the storage mount
`/fat32/projects/openxy`; MTP exposes that same folder as `/projects/openxy`.

Before **each** hardware run, remove both status files over MTP and verify they
are absent with `python sdk.py device clear-probe-status --record <private>/clear-001`.
This backs up and removes only SDK inspection and RPC JSON reports; it refuses other content.
After restarting, require two newly created reports whose platform
and digest match the packed file's `inspection.json`. A report left from an
earlier boot is not evidence that either role ran on the current boot; a failed
report write may leave an earlier file in place.

Build a static package with `--collection-probe`, then create its inspection file:

```sh
python sdk.py build --collection-probe --machine <folder> --stock-os <stock-1.2.1>/os.ldr --out <builds>/probe-001
python sdk.py collection pack --package <builds>/probe-001 --out <builds>/inspection-001
```

The latter's `candidate/projects/openxy/collection.oxc` is a development artifact,
not an installable machine. Keep both outputs outside every checkout. Probe
packages cannot be exported as browser kits.

The opt-in profile redirects one authenticated file-matcher call to an SDK
wrapper. It tries the original stock rule first and, at the two-level project
rule only, also accepts `/projects/openxy/` followed by a name made of word
characters or hyphens and ending in `.oxc`. Every existing rule stays unchanged.
Stock's small regex matcher treats groups and alternation as literal characters;
the added expression uses its supported syntax. After an
authorized, fully verified probe firmware installation, normal COM → M4 MTP
can transfer files in `/projects/openxy`; create that folder before testing a
missing collection. On Windows or macOS `sdk.py device push <builds>/inspection-001/candidate`
uses the existing backup, readback and rollback path. SDK collection files have
their own allowlist under `projects/openxy`; project documents cannot be pushed.

Complete [local native verification](#local-native-verification), or
[release verification](#release-verification) for a published package, before a
hardware test. Passing host checks does not establish startup filesystem
availability, timing or recovery. These diagnostic options never execute an
external collection.

`--collection-rpc-probe` includes collection inspection and holds both processor
roles at engine-initializer entry. GUI claims a boot-bound lease, checks all
identity words, freezes audio, verifies that copy and publish requests are
refused, then cancels. This diagnostic always resumes the embedded collection;
it has no executable-memory copy or publication backend.

Requests use stock's known close wire format with a negative invalid descriptor,
an epoch and a ticketed SDK marker. Ordinary requests keep their stock dispatcher.
The handler only exchanges mailbox data; the held startup task owns transitions.
A dedicated static GUI helper owns the transport notification and request for
the entire boot. Each initializer waits at most 10,000 delay ticks. Late replies
cannot change the latched result or activate code.

Clear old reports before each run and require fresh `gui-rpc-status.oxc` and
`audio-rpc-status.oxc` files with matching platform fingerprints and epochs.
Success is `hold-roundtrip-ok` on GUI and `hold-cancelled` on audio, with
`freeze-seen: true`, two destructive requests refused and `released-to: embedded`.
This measures initializer overlap; callback quiescence, cache synchronization
and durable rollback still require separate qualification.

### Resident RAM collections

`build --collection-loader` creates an opt-in resident kernel and embeds the
selected machines as its fallback. Add `--expanded-memory` to use the
[experimental 4 MiB reservation](builder.md#capacity-and-machine-numbers),
which leaves 16 MiB for the stock application heap. The kernel package records
the choice and every replacement collection inherits it. Switching the memory
layout requires building and installing a kernel for that layout.

After native verification, enter dev Mac MTP and copy only its OS and
protected fallback artwork:

```sh
python sdk.py device push <builds>/kernel-001/candidate --kernel-boot --record <private>/kernel-boot-001
```

Use the dev menu to write that verified OS to boot, then restart. In regular
COM → M4 MTP, install its information record in the user filesystem:

```sh
python sdk.py device push <builds>/kernel-001/candidate --kernel-record --record <private>/kernel-record-001
```

These scopes preserve the existing machine catalog and artwork. Subsequent collections use regular
COM → M4 MTP and a reboot; they do not write the boot partition. This mode
cannot be exported as a browser kit.

Keep the kernel package and its SDK source snapshot. Build a RAM collection
with exactly that SDK and the pinned tools:

```sh
python sdk.py collection executable --kernel <builds>/kernel-001 --out <builds>/collection-001
python sdk.py device install-collection <builds>/collection-001 --verify-only --skip-presets --record <private>/verify-transfer-001
```

Without a selection argument, the collection includes the embedded machines
and extensions. `--machine <folder>` adds a machine. `--previous <collection>`
starts from a previous RAM collection and retains its ID ledger;
`--remove <machine-id>` omits that machine. `--collection <file>` supplies a full
replacement selection. Removed IDs remain reserved in `layout.json`. Keep
extensions such as Capture in that selection.

Regular MTP collection installs preserve existing presets. Generated factory
presets remain in the build output, but their regular-MTP destination is not
qualified. A bundle containing them requires `--skip-presets`; no bundled
preset is transferred. The web manager uses the same omission and displays it
before applying changes.

The builder recompiles the complete collection, requires identical resident
bytes and exported addresses, and extracts only the fixed payload. Its immutable
artwork and catalog files use their full SHA-256 names. The installer backs up,
copies and reads back the inactive slot and its resources first, then writes
`activate.oxc` in a second transaction. A failed second transaction restores
the old selector. Existing content files and the active slot are preserved.

`--verify-only` requests file, parser and cache preflight at the next boot,
then resumes the embedded machines without copying external code. For an
activation run, reinstall without that flag to create a new generation:

```sh
python sdk.py device install-collection <builds>/collection-001 --skip-presets --record <private>/activate-transfer-001
python sdk.py device disable-collection --record <private>/disable-transfer-001
python sdk.py device collection-status
```

Reboot after either command. Disabling returns to the embedded selection;
removing one machine requires building and installing the replacement selection.
Files remain on storage so an interrupted transfer cannot remove rollback data.
Do not combine a selector and slot/resources in a generic `device push` folder:
activation must be its own final verified transaction.

Both processor initializers agree on one boot epoch and collection identity.
A pending attempt is flushed and read back before large allocations, native
parsing or executable writes. Failed preparation cleans its buffers before
resuming the embedded set. Once a copy is authorized, uncertainty keeps startup
held until a physical reset; it never returns through overwritten code. A
pending or corrupt attempt selects the embedded set on the next boot.
Confirmation requires completed registration, live audio readiness and fresh
GUI rendering after a 20-second startup window. This covers early startup,
not arbitrary later machine faults or every power-loss/storage failure. COM
at power-on still enters the stock dev boot menu for recovery. Qualify the
complete kernel and collection on hardware before treating this workflow as
accepted; the inspection and hold probes alone do not qualify activation.

## On the device

Each of these cost someone a round of installing:

- **Build from the device's layout.** Pass the `layout.json` of the firmware
  the OP-XY is running, which a builder install leaves at the device's root,
  not one from another build. Otherwise machines get other numbers, and
  projects saved without a [companion](projects.md) open the wrong machines.
- **Write the OS from the boot menu.** A plain restart keeps the old OS. If
  you have left file transfer, hold COM while switching on, then press track 3
  and track 5, and switch off and on after "firmware written ok!".
- **`producer_factory … not registered` is not proof.** The device's log in
  `/logs` shows it at every boot, even when every machine works. A machine
  missing from the menu is what shows the old OS is still running.
- **Copying never deletes.** Presets made for a machine's old controls stay on
  the device and sound wrong until you delete them. Factory presets of machines
  a build leaves out stay too; the firmware hides them
  ([presets](abi-1.2.1.md#presets)).
- **Effects get a quiet send.** An effect receives a much quieter signal than
  a synth's output ([effects](api.md#effects)): one tuned on the host blew out
  at a 1 to 2% send on the device. Test effects on the device at low send
  settings.
- **The stock LFO needs voices or a selected track.** It moves a machine that
  plays no voices only while that machine's track is selected
  ([#238](https://github.com/etcetc000/open-xy/issues/238)).

## What a build checks

| Check | Responsibility |
| --- | --- |
| Stock/tool hashes, exact edit intervals, entry/loader preservation, protected ranges | Shared platform/packager |
| Linked memory bounds, relocations, audio-callback recursion and stack depth, and complete output reconstruction | Shared infrastructure; the selected machines share one module limit |
| Callback layout, registration/name ownership, artwork format | SDK adapters and integration checks |
| Audio arithmetic, sound references, animation sequences and port fidelity | Selected engine's checks |

Host checks do not model device scheduling, interrupts, caches, stock graphics
or recovery, and do not guarantee a recoverable device.

## Local native verification

For development on any [pinned host](toolchain.md#hosts), use `--local` with a
package built on that same host:

```sh
python sdk.py check --local --native-package <builds>/mine-001 --stock-os <stock-1.2.1>/os.ldr --stock-content profiles/opxy-1.2.1/content
```

This authenticates the host's tools, independently rebuilds and compares the
package, and runs the complete browser and Python suites with every firmware
fixture required. Missing inputs, failed tests or any skipped test fail the
command. Set `--host-cxx` if the host C++ compiler is not `g++`.
`sdk.py verify --local` runs only the independent rebuild and comparison; it
does not run the fixture suites. Neither command changes the package's recorded
hashes or treats a package from another host as equivalent.

Local verification does not qualify published releases or establish device
timing, startup scheduling or recovery. Source changes require a fresh build.

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
