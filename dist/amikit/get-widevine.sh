#!/bin/sh
# get-widevine.sh - add Google's Widevine CDM (DRM: Netflix, Spotify, Prime Video, Disney+ ...) to the AmiKit engine.
# Lives in AmiSurf/Host/. Run it once on Linux:  sh get-widevine.sh [--yes] [--from /opt/google/chrome | --download]
# The Widevine CDM belongs to Google and may not be redistributed, so AmiSurf does not ship it.
# This script takes it from Google Chrome: from a Chrome already installed on this PC (/opt/google/chrome), or by
# downloading Google's Chrome package from dl.google.com and taking only its WidevineCdm folder (nothing is installed).
# It goes to Host/data/WidevineCdm/<version>; the engine picks it up at its next start.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
YES=0; FROM=""; DL=0
while [ $# -gt 0 ]; do case "$1" in --yes) YES=1 ;; --from) FROM=$2; shift ;; --download) DL=1 ;; -h|--help) sed -n '2,7p' "$0"; exit 0 ;; esac; shift; done
. "$HERE/asb-state.sh"
DEST="$STATE/WidevineCdm"
say(){ echo "$*"; echo "get-widevine: $*" >>"$LOG"; }
ok(){ [ -f "$1/manifest.json" ] && [ -f "$1/_platform_specific/linux_x64/libwidevinecdm.so" ]; }
ver(){ sed -n 's/.*"version"[^"]*"\([^"]*\)".*/\1/p' "$1/manifest.json" | head -1; }
put(){   # copy CDM folder $1 into $DEST/<version>, make it the current one
  v=$(ver "$1"); [ -n "$v" ] || { say "no version in $1/manifest.json"; exit 1; }
  mkdir -p "$DEST" && rm -rf "$DEST/$v.tmp" && cp -R "$1" "$DEST/$v.tmp" && rm -rf "$DEST/$v" && mv "$DEST/$v.tmp" "$DEST/$v" || { say "cannot write $DEST"; exit 1; }
  chmod 755 "$DEST/$v/_platform_specific/linux_x64/libwidevinecdm.so"
  rm -f "$DEST/current"; printf '%s\n' "$v" >"$DEST/current.txt"
  for o in "$DEST"/*; do [ -d "$o" ] && [ "$o" != "$DEST/$v" ] && rm -rf "$o"; done
  say "Widevine $v installed in $DEST/$v. Restart the engine (or AmiSurf) to use it."; exit 0; }
if [ -z "$FROM" ] && [ $DL = 0 ]; then for d in /opt/google/chrome /opt/google/chrome-beta /opt/google/chrome-unstable; do ok "$d/WidevineCdm" && { FROM=$d; break; }; done; fi
if [ -n "$FROM" ]; then ok "$FROM/WidevineCdm" || { say "no WidevineCdm in $FROM"; exit 1; }; say "using Widevine from $FROM"; put "$FROM/WidevineCdm"; fi
URL=https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb
echo "Google Chrome is not installed on this PC. This script can download Google Chrome's Linux package"
echo "(about 120 MB) from $URL"
echo "and copy only its Widevine CDM. The CDM is Google's software under Google's terms (its LICENSE file is"
echo "copied along); by continuing you obtain it from Google for your own use."
if [ $YES = 0 ]; then printf "Continue? [y/N] "; read a; case "$a" in y|Y|yes) ;; *) echo "Nothing done."; exit 1 ;; esac; fi
mkdir -p "$STATE/tmp"; T=$(mktemp -d "$STATE/tmp/widevine.XXXXXX") || exit 1; trap 'rm -rf "$T"' EXIT
command -v curl >/dev/null 2>&1 && G="curl -fL --retry 2 -o" || G="wget -O"
say "downloading $URL"; $G "$T/c.deb" "$URL" || { say "download failed"; exit 1; }
cd "$T" || exit 1
if command -v ar >/dev/null 2>&1; then ar x c.deb data.tar.xz
elif command -v bsdtar >/dev/null 2>&1; then bsdtar -xf c.deb data.tar.xz
elif command -v python3 >/dev/null 2>&1; then python3 - <<'PY'
import sys
f=open('c.deb','rb'); assert f.read(8)==b'!<arch>\n'
while True:
    h=f.read(60)
    if len(h)<60: sys.exit('no data.tar.xz')
    name=h[:16].decode().strip().rstrip('/'); size=int(h[48:58]); body=f.read(size)
    if size%2: f.read(1)
    if name.startswith('data.tar'): open('data.tar.xz','wb').write(body); break
PY
else say "need ar (binutils), bsdtar or python3 to unpack the package"; exit 1; fi
[ -f data.tar.xz ] || { say "unexpected package layout"; exit 1; }
tar -xJf data.tar.xz ./opt/google/chrome/WidevineCdm || { say "no WidevineCdm in the package (needs xz)"; exit 1; }
ok "$T/opt/google/chrome/WidevineCdm" || { say "WidevineCdm incomplete in the package"; exit 1; }
put "$T/opt/google/chrome/WidevineCdm"
