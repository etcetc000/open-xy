# The pinned Linux build environment for tools/gcc/build-linux.sh.
#
#   docker build -t open-xy-toolchain-linux - < tools/gcc/linux.Dockerfile
#
# Debian packages come from one snapshot.debian.org date, so the image and the
# host compilers it records can be rebuilt after the mirrors move on.
FROM debian:bookworm-20251020@sha256:26f2a7cab45014541c65f9d140ccfa6aaefbb49686c6759bea9c6f7f5bb3d72f
RUN rm -f /etc/apt/sources.list.d/* && \
    printf '%s\n' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20251020T000000Z bookworm main' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20251020T000000Z bookworm-updates main' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/20251020T000000Z bookworm-security main' \
      > /etc/apt/sources.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends gcc g++ make bison flex m4 git \
      libgmp-dev libmpfr-dev libmpc-dev && \
    rm -rf /var/lib/apt/lists/*
