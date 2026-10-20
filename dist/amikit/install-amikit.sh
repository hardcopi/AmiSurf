#!/bin/bash
# install-amikit.sh - optional Linux-side setup for the engine in AmiSurf/Host: makes sure bsdsocket
# emulation is on in AmiKit's emulator configs and adds a desktop entry "AmiKit + AmiSurf".
# Not needed when AmiKit's Rabbit Hole (C:host-run) is set up: AmiSurf then starts Host/run-host.sh itself.
#   ./install-amikit.sh                 AmiKit in ~/AmiKit (or give the folder: ./install-amikit.sh /path/to/AmiKit)
#   ./install-amikit.sh --no-config     do not touch any emulator config
#   ./install-amikit.sh --uninstall     remove the desktop entry (configs keep their .asb-backup files)
#   --widevine / --no-widevine          add Google's Widevine CDM (DRM video) without asking / never ask
# Every config that is changed is first copied to <file>.asb-backup-YYYYmmdd-HHMMSS.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
AK=""; NOCONF=0; UNINST=0; WV=ask
for a in "$@"; do case "$a" in --no-config) NOCONF=1 ;; --uninstall) UNINST=1 ;; --widevine) WV=yes ;; --no-widevine) WV=no ;; -h|--help) sed -n '2,7p' "$0"; exit 0 ;; *) AK=$a ;; esac; done
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"; DESK="$APPS/amikit-with-browser.desktop"
if [ $UNINST = 1 ]; then rm -f "$DESK"; echo "removed $DESK"; exit 0; fi
if [ -z "$AK" ]; then for d in "$HOME/AmiKit" "$HOME/Desktop/AmiKit" "$HOME/Applications/AmiKit" /opt/AmiKit "$HERE/../.."; do [ -d "$d" ] && { AK=$d; break; }; done; fi
[ -n "$AK" ] && [ -d "$AK" ] || { echo "AmiKit folder not found; run: $0 /path/to/AmiKit"; exit 1; }
AK=$(cd "$AK" && pwd)

# 1. the engine stays where it is (AmiSurf/Host); just make sure its programs are executable
DEST="$HERE"; sh "$HERE/run-host.sh" --help >/dev/null 2>&1
for f in amistudio-hostd asb-chromed engine.sh chromium/chrome chromium/chrome_crashpad_handler amikit-with-browser.sh run-host.sh \
         amistudio-host/amistudio-hostd amistudio-host/engine/engine.sh amistudio-host/engine/asb-chromed amistudio-host/engine/chromium/chrome amistudio-host/engine/chromium/chrome_crashpad_handler; do [ -f "$DEST/$f" ] && chmod +x "$DEST/$f"; done

# 2. desktop entry
mkdir -p "$APPS"
cat >"$DESK" <<D
[Desktop Entry]
Type=Application
Name=AmiKit + AmiSurf
Comment=AmiKit with the AmiStudio Host for AmiSurf (Chromium) on 127.0.0.1:9220
Exec=env AMIKIT_DIR="$AK" "$DEST/amikit-with-browser.sh"
Path=$AK
Terminal=false
Categories=Game;Emulator;
D
for i in "$DEST/chromium/product_logo_48.png" "$DEST/amistudio-host/engine/chromium/product_logo_48.png"; do [ -f "$i" ] && { echo "Icon=$i" >>"$DESK"; break; }; done
chmod +x "$DESK"; echo "desktop entry: $DESK"

# 2b. (0.80) Widevine CDM for DRM video (Netflix, Spotify, Prime Video, Disney+). Google's, never shipped with
# AmiSurf: get-widevine.sh copies it from Google Chrome or downloads it from Google. Optional.
if [ -f "$DEST/get-widevine.sh" ] && [ $WV != no ] && [ ! -f "$DEST/data/WidevineCdm/current.txt" ] && [ ! -f /opt/google/chrome/WidevineCdm/manifest.json ]; then
  if [ $WV = yes ]; then sh "$DEST/get-widevine.sh" --yes || echo "Widevine not added (you can run $DEST/get-widevine.sh later)"
  elif [ -t 0 ]; then
    printf "Add Google's Widevine CDM for DRM video (Netflix, Spotify, ...)? It is downloaded from Google. [y/N] "; read a
    case "$a" in y|Y|yes) sh "$DEST/get-widevine.sh" --yes || echo "Widevine not added (you can run $DEST/get-widevine.sh later)" ;;
      *) echo "Widevine skipped; for DRM sites run: sh $DEST/get-widevine.sh" ;; esac
  else echo "for DRM sites (Netflix, Spotify ...) run once: sh $DEST/get-widevine.sh"; fi
fi

# 3. bsdsocket emulation (key bsdsocket_emu, the same in Amiberry, FS-UAE-style .uae and WinUAE configs)
[ $NOCONF = 1 ] && exit 0
STAMP=$(date +%Y%m%d-%H%M%S); n=0
CANDS=$(find "$AK" -maxdepth 4 -iname '*.uae' 2>/dev/null
        for c in "$HOME/.config/amiberry/conf" "$HOME/Amiberry/conf" "$HOME/.var/app/com.blitterstudio.amiberry/config/amiberry/conf"; do
          [ -d "$c" ] && find "$c" -maxdepth 1 -iname '*amikit*.uae' 2>/dev/null; done)
IFS='
'
for f in $CANDS; do
  [ -f "$f" ] || continue
  grep -qiE '^[[:space:]]*bsdsocket_emu[[:space:]]*=[[:space:]]*(true|yes|1)[[:space:]]*$' "$f" && { echo "bsdsocket already on: $f"; continue; }
  cp -p "$f" "$f.asb-backup-$STAMP" || { echo "cannot back up $f, leaving it alone"; continue; }
  if grep -qiE '^[[:space:]]*bsdsocket_emu[[:space:]]*=' "$f"; then
    sed -i -E 's/^([[:space:]]*bsdsocket_emu[[:space:]]*=)[[:blank:]]*[[:alnum:]]*/\1true/I' "$f"   # keeps CRLF line ends
  else
    case "$(head -c 4096 "$f")" in *$'\r'*) printf 'bsdsocket_emu=true\r\n' >>"$f" ;; *) printf 'bsdsocket_emu=true\n' >>"$f" ;; esac
  fi
  echo "bsdsocket enabled: $f (backup $f.asb-backup-$STAMP)"; n=$((n+1))
done
[ -z "$CANDS" ] && echo "no AmiKit emulator config (.uae) found; if networking is off, enable bsdsocket.library in the emulator's settings (Amiberry: Expansions > Network)."
echo "done ($n config(s) changed). Start \"AmiKit + AmiSurf\" from your menu, or $DEST/amikit-with-browser.sh"
