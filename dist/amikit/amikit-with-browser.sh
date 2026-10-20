#!/bin/bash
# amikit-with-browser.sh - start the bundled AmiStudio Host on 127.0.0.1:9220 (loopback, no pairing),
# run AmiKit, and stop the Host and its engine (by the Host's exact PID) when AmiKit exits.
#   ./amikit-with-browser.sh                 find AmiKit in ~/AmiKit (or $AMIKIT_DIR) and run it
#   AMIKIT_CMD="/path/to/AmiKit launcher" ./amikit-with-browser.sh   run exactly this instead
#   ./amikit-with-browser.sh --host-only     just run the host (Ctrl-C to stop), e.g. for a hand-started emulator
# options: --port N (default 9220)   --visible (show Chromium on your desktop instead of headless)
#          --lan (also serve real Amigas on the network; they pair once: sh run-host.sh --pair)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=9220; HOSTONLY=0; VISIBLE=0; LAN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --port) PORT=$2; shift ;;
    --host-only) HOSTONLY=1 ;;
    --visible) VISIBLE=1 ;;
    --lan) LAN=1 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown option $1 (see --help)"; exit 2 ;;
  esac; shift
done
. "$HERE/asb-state.sh"   # CHROME (Host/amistudio-host/engine/chromium or Host/chromium), STATE (default Host/data, inside the drawer) and LOG

# Chromium's system libraries: present on every desktop Linux; say exactly what is missing otherwise
MISSING=$(LD_LIBRARY_PATH="$(dirname "$CHROME")" ldd "$CHROME" 2>/dev/null | sed -n 's/^[[:space:]]*\([^ ]*\) => not found.*/\1/p' | tr '\n' ' ')
if [ -n "$MISSING" ]; then
  echo "The bundled Chromium needs these system libraries, which this PC lacks: $MISSING"
  echo "Debian/Ubuntu: sudo apt install libnss3 libatk-bridge2.0-0 libgbm1 libasound2 libcups2 libxkbcommon0 libxcomposite1 libxdamage1 libxrandr2 libpango-1.0-0"
  echo "Fedora: sudo dnf install nss atk at-spi2-atk mesa-libgbm alsa-lib cups-libs libxkbcommon pango"
  exit 1
fi

find_amikit(){
  [ -n "${AMIKIT_CMD:-}" ] && { echo "$AMIKIT_CMD"; return 0; }
  for d in ${AMIKIT_DIR:-} "$HOME/AmiKit" "$HOME/Desktop/AmiKit" "$HOME/Applications/AmiKit" /opt/AmiKit "$HERE/.." "$HERE/../.." "$HERE/../../.."; do
    [ -d "$d" ] || continue
    for f in "$d/AmiKit" "$d/AmiKit.sh" "$d/start-amikit.sh" "$d/AmiKit.AppImage" "$d"/AmiKit*.AppImage "$d"/*AmiKit*Launcher*; do
      [ -f "$f" ] && [ -x "$f" ] && [ "$f" != "$HERE/amikit-with-browser.sh" ] && { echo "$f"; return 0; }
    done
  done
  return 1
}
if [ $HOSTONLY = 0 ]; then
  AK=$(find_amikit) || { echo "AmiKit launcher not found. Set AMIKIT_DIR=/path/to/AmiKit or AMIKIT_CMD=\"command that starts AmiKit\", or use --host-only."; exit 1; }
fi

# the bundled AmiStudio Host on 127.0.0.1 (an emulator on this PC needs no pairing); it starts the engine
# (asb-chromed + the bundled Chromium, headless) when AmiSurf connects. Stopped by its exact PID when AmiKit exits.
set -- --foreground --idle 0 --port "$PORT"; [ $VISIBLE = 1 ] && set -- "$@" --visible; [ $LAN = 1 ] && set -- "$@" --lan
sh "$HERE/run-host.sh" "$@" & HPID=$!   # run-host.sh --foreground waits for amistudio-hostd and passes TERM on to its PID (0.86)
stop_host(){ if kill -0 "$HPID" 2>/dev/null; then kill "$HPID" 2>/dev/null; wait "$HPID" 2>/dev/null; fi; }
trap 'stop_host' EXIT; trap 'exit 130' INT TERM
i=0; until (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null || [ $i -ge 40 ]; do
  kill -0 "$HPID" 2>/dev/null || { echo "the AmiStudio Host did not start (or another program has port $PORT), see $LOG"; tail -5 "$LOG"; exit 1; }; sleep 0.25; i=$((i+1)); done 2>/dev/null
echo "AmiStudio Host for AmiSurf running on 127.0.0.1:$PORT (PID $HPID, log $LOG)"
if [ $HOSTONLY = 1 ]; then echo "Press Ctrl-C to stop."; wait "$HPID"; exit 0; fi
echo "Starting AmiKit: $AK"
if [ -n "${AMIKIT_CMD:-}" ]; then sh -c "$AMIKIT_CMD"; else (cd "$(dirname "$AK")" && "$AK"); fi; RC=$?
echo "AmiKit exited ($RC); stopping the AmiStudio Host PID $HPID"
exit $RC
