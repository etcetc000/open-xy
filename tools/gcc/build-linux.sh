#!/usr/bin/env bash
# Build the Blackfin binutils and GCC bundle on Linux x86_64, in the pinned
# container from tools/gcc/linux.Dockerfile.
#
#   docker build -t open-xy-toolchain-linux - < tools/gcc/linux.Dockerfile
#   docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
#     -v <open-xy-checkout>:/opt/opxy-toolchain/open-xy:ro \
#     -v <fork-checkout>:/opt/opxy-toolchain/fork \
#     -v <output-parent>:/opt/opxy-toolchain/out \
#     open-xy-toolchain-linux /opt/opxy-toolchain/open-xy/tools/gcc/build-linux.sh \
#     /opt/opxy-toolchain/fork /opt/opxy-toolchain/out/blackfin
#
# <fork-checkout>  deanm1278/blackfin-plus-gnu at e4408a15b9be6cd911283f103a1a3f69e9a6b8c0
#                  (only its gcc/ and binutils-gdb/ trees are used)
# <output>         new directory: `--toolchain <output>/bin`, `--compiler <output>`
#
# Applies the GCC codegen fix (patches/0001) and the disassembler's 64-bit
# `long` fix (patches/macos/0003, needed on any LP64 host). gmp, mpfr, mpc and
# the host C++ runtime are linked statically, so the tools need only glibc
# 2.36 or later. Source and install paths are compiled into the tools; the
# container fixes both, so a rebuild with the paths above reproduces the same
# files wherever the checkouts live. The build directory is <output>.build.
set -euo pipefail
src=$(cd "$1" && pwd); out=$2
repo=$(cd "$(dirname "$0")/../.." && pwd)
[ ! -e "$out" ] || { echo "output must not exist: $out" >&2; exit 1; }
for pinned in 'gcc-12 12.2.0-14+deb12u1' 'g++-12 12.2.0-14+deb12u1' 'bison 2:3.8.2+dfsg-1+b1' \
              'libgmp-dev 2:6.2.1+dfsg1-1.1' 'libmpfr-dev 4.2.0-1' 'libmpc-dev 1.3.1-1'; do
  [ "$(dpkg-query -W -f='${Version}' "${pinned%% *}")" = "${pinned#* }" ] ||
    { echo "${pinned%% *} must be ${pinned#* } (build in tools/gcc/linux.Dockerfile)" >&2; exit 1; }
done
gcc --version | head -n 1
mkdir -p "$out"; out=$(cd "$out" && pwd)
work=$out.build; rm -rf "$work"; mkdir "$work"  # this script's own build directory
export PATH="$out/bin:/usr/bin:/bin"
jobs=$(nproc)

for patch in "$repo"/tools/gcc/patches/0001-*.patch "$repo"/tools/gcc/patches/macos/0003-*.patch; do
  # Paths are relative to the fork root; a rerun keeps already applied patches.
  git -C "$src" apply --ignore-whitespace -R --check "$patch" 2>/dev/null ||
    git -C "$src" apply --ignore-whitespace "$patch"
done
# The fork records its build helper scripts without the executable bit.
find "$src/binutils-gdb" "$src/gcc" -type f \( -name '*.sh' -o -name install-sh -o -name mkinstalldirs \
  -o -name move-if-change -o -name missing -o -name ylwrap -o -name depcomp -o -name genmultilib \) \
  -exec chmod +x {} +

mkdir "$work/binutils" && cd "$work/binutils"
"$src/binutils-gdb/configure" --target=bfin-elf --prefix="$out" \
  --disable-werror --disable-nls --disable-gdb --disable-sim --disable-gprof --disable-gold \
  --without-zlib --without-zstd CFLAGS=-O2 CXXFLAGS=-O2 LDFLAGS=-static-libgcc MAKEINFO=true
make -j"$jobs" all-binutils all-gas all-ld MAKEINFO=true
for dir in binutils gas ld; do make -C "$dir" install-exec MAKEINFO=true; done

# Static gmp/mpfr/mpc: a prefix holding only their headers and archives.
mkdir -p "$work/static/include" "$work/static/lib"
ln -s /usr/include/x86_64-linux-gnu/gmp.h /usr/include/mpfr.h /usr/include/mpf2mpfr.h \
  /usr/include/mpc.h "$work/static/include/"
for lib in gmp mpfr mpc; do ln -s "/usr/lib/x86_64-linux-gnu/lib$lib.a" "$work/static/lib/"; done

mkdir "$work/gcc" && cd "$work/gcc"
"$src/gcc/configure" --target=bfin-elf \
  --prefix="$out" --enable-languages=c,c++ \
  --without-headers --with-newlib --disable-shared --disable-threads \
  --disable-libssp --disable-libquadmath --disable-libgomp --disable-libatomic \
  --disable-nls --disable-werror --disable-multilib --disable-lto --without-isl \
  --disable-plugin --disable-bootstrap \
  --with-gmp="$work/static" --with-mpfr="$work/static" --with-mpc="$work/static" \
  --with-stage1-ldflags="-static-libstdc++ -static-libgcc" \
  CC=gcc CXX=g++ CFLAGS="-O2 -std=gnu11" \
  CXXFLAGS="-O2 -std=gnu++14 -Wno-narrowing" MAKEINFO=true
make -j"$jobs" all-gcc MAKEINFO=true || {
  # Patch 0001 changes target hooks documentation; accept the regenerated copy.
  cp gcc/tm.texi "$src/gcc/gcc/doc/tm.texi"
  make -j"$jobs" all-gcc MAKEINFO=true
}
make install-gcc MAKEINFO=true

rm -rf "$work"
echo "Tools: $out/bin  Bundle: $out (record reviewed hashes in profiles/toolchains.json before use)"
