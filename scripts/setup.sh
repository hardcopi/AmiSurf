#!/bin/bash
# scripts/setup.sh - set up a Debian/Ubuntu machine to build and test AmiSurf.
#   Installs host packages (SDL2, SDL2_ttf, Xvfb, ffmpeg, lha, fs-uae)
#   and builds the bebbo m68k-amigaos-gcc toolchain into /opt/amiga
#   (skipped if /opt/amiga/bin/m68k-amigaos-gcc already exists; the build takes 20-60 min).
# Options: --no-toolchain  skip the Amiga cross compiler   --no-emu  skip fs-uae
# It never downloads Kickstart ROMs or AmigaOS files: you must supply your own (see DEVELOPING.md).
set -euo pipefail
cd "$(dirname "$0")/.."
TOOLCHAIN=1; EMU=1
for a in "$@"; do case $a in --no-toolchain) TOOLCHAIN=0;; --no-emu) EMU=0;; esac; done
SUDO=; [ "$(id -u)" = 0 ] || SUDO=sudo
echo "== host packages"
$SUDO apt-get update
$SUDO apt-get install -y build-essential git perl pkg-config libsdl2-dev libsdl2-ttf-dev fonts-liberation \
  xvfb ffmpeg lhasa python3 curl
# lha: lhasa gives 'lha' for extraction; jlha-utils (if available) can also create archives for releases
$SUDO apt-get install -y jlha-utils || echo "(jlha-utils not available: use any LHA archiver to build release archives)"
if [ $EMU = 1 ]; then $SUDO apt-get install -y fs-uae || echo "(fs-uae not in apt: install from https://fs-uae.net or use Amiberry)"; fi
if ! command -v google-chrome >/dev/null && ! command -v google-chrome-stable >/dev/null && ! command -v chromium >/dev/null; then
  echo "(install Google Chrome or Chromium for asb-chromed)"; fi
if [ $TOOLCHAIN = 1 ] && [ ! -x /opt/amiga/bin/m68k-amigaos-gcc ]; then
  echo "== bebbo amiga-gcc (m68k-amigaos-gcc) -> /opt/amiga"
  $SUDO apt-get install -y make wget lhasa git subversion texinfo gettext bison flex libgmp-dev libmpfr-dev libmpc-dev rsync \
    libncurses-dev automake autoconf libtool g++ gperf
  $SUDO mkdir -p /opt/amiga; $SUDO chown "$(id -u):$(id -g)" /opt/amiga
  T=$(mktemp -d); git clone --depth 1 https://github.com/bebbo/amiga-gcc "$T/amiga-gcc"
  ( cd "$T/amiga-gcc" && make update && make all -j"$(nproc)" PREFIX=/opt/amiga )
  rm -rf "$T"
fi
echo "== build + tests"
make host
[ -x /opt/amiga/bin/m68k-amigaos-gcc ] && make amiga || echo "(no cross compiler: Amiga build skipped)"
make test
echo "setup done. Emulator: see DEVELOPING.md (bring your own Kickstart/AmigaOS)."
