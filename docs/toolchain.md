# Toolchains

| Task | Needs | Platforms |
| --- | --- | --- |
| Browser preview (`sdk.py preview`, `new`) | Python 3.11+, NumPy, Node 22.12+, Clang with the wasm32 target | Any |
| SDK checks (`sdk.py check`) | Python 3.11+, Node 22.12+ | Any |
| Native firmware (`sdk.py compile`, `build`) | The pinned Blackfin+ binutils and GCC 8 bundle below | Pinned hosts below |
| Release browser kit (`sdk.py kit`) | The same, on the release host | Windows x86_64 |

`python sdk.py doctor --preview` checks the preview tools, and
`python sdk.py doctor --toolchain [<dir>]` checks the native binutils.
`python sdk.py toolchain fetch` installs the native tools (below).

## Native tools

The OP-XY CPU is a Blackfin+ (BF707). The native tools come from the
Blackfin+ GNU fork <https://github.com/deanm1278/blackfin-plus-gnu> at commit
`e4408a15b9be6cd911283f103a1a3f69e9a6b8c0`.

| Argument | Contents | Pinned in |
| --- | --- | --- |
| `--toolchain` | `bfin-elf-as`, `-ld`, `-objcopy`, `-objdump` | `tools` of the host in [`profiles/toolchains.json`](../profiles/toolchains.json) |
| `--compiler` | GCC 8 C++ for every machine | `compiler` of the host in the same file |

The fork's GCC needs one code-generation fix for correct output.
[`0001-bfin-loop-end-clobbers-cc.patch`](../tools/gcc/patches/0001-bfin-loop-end-clobbers-cc.patch)
marks the condition flag as clobbered when a hardware loop falls back to a
decrement-and-branch. Without it, GCC can keep a live comparison across that
branch and miscompile the code.
[`tools/gcc/build-gcc.sh`](../tools/gcc/build-gcc.sh) builds the `--compiler`
bundle with that fix under MSYS2 MinGW64.
The `0002` patch only lets the compiler itself build on a Windows host.
[`tools/gcc/build-macos.sh`](../tools/gcc/build-macos.sh) builds both on macOS
with the host fixes in `tools/gcc/patches/macos`: the bundled zlib on current
SDKs, Apple silicon host hooks, and a disassembler sign extension that assumed
a 32-bit `long`.
[`tools/gcc/build-linux.sh`](../tools/gcc/build-linux.sh) builds both on Linux
x86_64 inside the container defined by
[`tools/gcc/linux.Dockerfile`](../tools/gcc/linux.Dockerfile), a Debian
bookworm image whose packages come from one snapshot.debian.org date. It
applies `0001` and the macOS disassembler fix, which every 64-bit host needs,
and links gmp, mpfr, mpc and the C++ runtime statically, so the tools need
only glibc 2.36 or later. The container fixes the source and install paths, so
a rebuild reproduces the pinned files wherever the checkouts live
([build steps](build.md#on-linux)).

## Hosts

| Host | Compiler built by | Output |
| --- | --- | --- |
| `windows-x86_64` (release) | `build-gcc.sh` | Reference module bytes |
| `darwin-arm64` | `build-macos.sh` | Valid module; not byte-identical to the reference |
| `linux-x86_64` | `build-linux.sh` | Valid module; not byte-identical to the reference |

GCC 8 sorts with the host C library's `qsort`, which orders equal elements
differently on each host, so some units get a different but equally valid
instruction order. The binutils produce identical bytes on Windows and macOS.
Published browser kits therefore come only from the release host named in
[`profiles/toolchains.json`](../profiles/toolchains.json); `sdk.py kit` refuses
on any other host.

Every tool file is hash-checked before it runs. Source and install paths are
compiled into the tools, so a rebuild matches its pins only at the same paths.
A host's `build` record in `profiles/toolchains.json`, where present, names the
script, source and output paths and host tools its pins were built with; `build-macos.sh`
refuses other Homebrew versions and `build-linux.sh` other Debian package
versions. A rebuilt tool gets a new reviewed entry and
a new `TOOLCHAINS_SHA256`; never relax a hash to admit it.

## Release downloads

`python sdk.py toolchain fetch` downloads this host's archive, checks its
hash and every pinned file, and installs it in `local/toolchain`. Native
commands use that folder when `--toolchain` and `--compiler` are omitted.
`--archive <zip>` installs an archive downloaded by hand. The archive URL and
hash are part of the host's reviewed pins (`download` in
`profiles/toolchains.json`). `linux-x86_64` has no release archive yet: build
its tools into `local/toolchain` ([on Linux](build.md#on-linux)).

Each downloadable host's tools are published as assets of this repository's
`toolchain-1` GitHub release:

| Asset | Contents |
| --- | --- |
| `open-xy-toolchain-<host>.zip` | One folder: the pinned binutils and compiler files, the runtime libraries they load, license texts and `SOURCES.md`. Pass it as `--compiler` and its `bin` as `--toolchain`. |
| `open-xy-toolchain-source-<host>.tar` | The corresponding source: the fork at the pinned commit, `tools/gcc` and the source package of every bundled runtime library. |
| `SHA256SUMS` | Hashes of both assets. |

The SDK hash-checks every pinned file wherever the folder is unpacked; the
zip needs no other software on the host. The macOS compiler links gmp, mpfr
and mpc statically and loads only system libraries. Its binaries are ad-hoc
signed, so macOS refuses copies unpacked by Finder from a browser download;
install with `sdk.py toolchain fetch`, which never marks files as
downloaded, passing `--archive` for a zip downloaded by hand. A maintainer on the pinned host
builds the assets with
[`tools/gcc/package_release.py`](../tools/gcc/package_release.py), which takes
the reviewed bundle, a checkout of the fork and the runtime source packages
listed with their hashes in
[`tools/gcc/release-sources.json`](../tools/gcc/release-sources.json). Both
assets are reproducible from the same inputs.
