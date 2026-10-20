#!/bin/sh
# run-host.sh - start the bundled AmiStudio Host (amistudio-hostd) on 127.0.0.1:9220 for AmiSurf (0.83).
# 0.86: self-contained - the Host, its engine and Chromium keep everything under Host/data (settings, tokens, profile,
# HOME, XDG folders, TMPDIR; see asb-state.sh); only downloads go to your real ~/Downloads.
# Lives in AmiSurf/Host/. AmiSurf runs it by itself through AmiKit's Rabbit Hole
# (C:host-run sh ".../Host/run-host.sh" --background --port 9220 --idle 30); you can also run it by hand on Linux.
#   sh run-host.sh [--idle MIN] [--background|--foreground] [--visible] [--lan] [--legacy [--port 9223]]
# The Host starts the browser engine (asb-chromed + the bundled Chromium, headless) when AmiSurf connects.
# --idle MIN   stop the engine, and the Host, after MIN minutes without an Amiga client (0 = never; the Host's
#              --exit-idle, 0.1.3+: no client connection open or opened for MIN minutes)
# --foreground stay in this terminal (Ctrl-C stops Host and engine); default (--background): detach, return at once.
#              (--background is accepted so one command line works for this script and the standalone Host 0.1.2's.)
# --visible    show Chromium on this desktop instead of headless
# --lan        also serve Amigas on the network (they pair once: sh run-host.sh --pair prints the code)
# --pair       print a pairing code for an Amiga on the network (Host must run with --lan)
# --legacy     the 0.82-and-older way: plain asb-chromed on 127.0.0.1:9223, no Host (AmiSurf HOSTMODE=LEGACY)
# Safe to run twice: if the port already answers it just exits.
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=""; IDLE=0; FG=0; VISIBLE=0; LAN=0; LEGACY=0; PAIR=0
while [ $# -gt 0 ]; do
  case "$1" in
    --port) PORT=$2; shift ;; --idle) IDLE=$2; shift ;;
    --foreground) FG=1 ;; --background) FG=0 ;; --visible) VISIBLE=1 ;; --lan) LAN=1 ;; --legacy) LEGACY=1 ;; --pair) PAIR=1 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
  esac; shift
done
. "$HERE/asb-state.sh"   # STATE (default Host/data, inside the drawer) and LOG
asb_selfcontained        # HOME, XDG_*, TMPDIR under STATE (0.86); ASB_DOWNLOADS = the real ~/Downloads
say(){ echo "$*"; echo "run-host: $*" >>"$LOG"; }
port_busy(){   # something listens on 127.0.0.1:$1 (nc, else bash's /dev/tcp, else /proc/net/tcp)
  if command -v nc >/dev/null 2>&1; then nc -z 127.0.0.1 "$1" 2>/dev/null; return; fi
  if command -v bash >/dev/null 2>&1; then bash -c "exec 3<>/dev/tcp/127.0.0.1/$1" 2>/dev/null; return; fi
  h=$(printf ':%04X ' "$1"); grep -q "$h.* 0A " /proc/net/tcp 2>/dev/null; }
# LHA, zip, Amiga file copies and some host directory drives drop the Unix exec bits: put them back
for f in amistudio-hostd asb-chromed engine.sh move-to-host.sh chromium/chrome chromium/chrome_crashpad_handler chromium/chrome-wrapper amikit-with-browser.sh install-amikit.sh get-widevine.sh \
         amistudio-host/amistudio-hostd amistudio-host/ash-cli amistudio-host/run-host.sh amistudio-host/install-user.sh amistudio-host/engine/engine.sh \
         amistudio-host/engine/asb-chromed amistudio-host/engine/chromium/chrome amistudio-host/engine/chromium/chrome_crashpad_handler amistudio-host/engine/chromium/chrome-wrapper; do
  [ -f "$HERE/$f" ] && [ ! -x "$HERE/$f" ] && chmod +x "$HERE/$f" 2>/dev/null && say "restored exec bit on $f"
done
HCONF="$STATE/hostd"   # the bundled Host's own folder (host.conf, tokens, browser.log) - not ~/.config/amistudio-host
# our own Host (its pid file) still runs: then the tmp link is in use and stays
hostd_alive(){ [ -f "$HCONF/hostd.pid" ] && kill -0 "$(cat "$HCONF/hostd.pid" 2>/dev/null)" 2>/dev/null; }
if [ $PAIR = 1 ]; then hostd_alive || asb_droplink; exec "$HOSTD" pair --confdir "$HCONF"; fi
[ -x "$ENG/asb-chromed" ] || { say "cannot make $ENG/asb-chromed executable (read-only or noexec drive?)"; exit 1; }
[ $LEGACY = 1 ] || [ -x "$HOSTD" ] || { say "cannot make $HOSTD executable (read-only or noexec drive?)"; exit 1; }
[ -n "$PORT" ] || { [ $LEGACY = 1 ] && PORT=9223 || PORT=9220; }
find "$STATE/tmp" -mindepth 1 -maxdepth 1 -mtime +1 -exec rm -rf {} + 2>/dev/null   # Chromium's leftovers from earlier runs
# already running? Never start a second Host: the AmiStudio launcher (0.1.4+, Rabbit Hole) or an installed Host may
# have started one. "amistudio-hostd status" asks over ASH; anything else on the port is reported, not fought over.
# (Host 0.1.3+: "status" gives up after 2 s on a port that is not a Host; ASH_TIMEOUT_MS overrides)
if [ $LEGACY = 0 ] && [ -x "$HOSTD" ] && ST=$("$HOSTD" status "127.0.0.1:$PORT" 2>/dev/null); then
  say "an AmiStudio Host already answers on 127.0.0.1:$PORT ($(echo "$ST" | sed -n 's/^name //p'), host $(echo "$ST" | sed -n 's/^host //p')) - not starting another"; hostd_alive || asb_droplink; exit 0; fi
if port_busy "$PORT"; then
  if [ $LEGACY = 1 ]; then say "already running on port $PORT"; exit 0; fi
  hostd_alive || asb_droplink
  say "port $PORT is taken by a program that is not an AmiStudio Host - not starting (see README-AMIKIT.txt)"; exit 1; fi
MISSING=$(ldd "$CHROME" 2>/dev/null | sed -n 's/^[[:space:]]*\([^ ]*\) => not found.*/\1/p' | tr '\n' ' ')
[ -n "$MISSING" ] && { say "Chromium needs system libraries this PC lacks: $MISSING (see README-AMIKIT.txt)"; asb_droplink; exit 1; }
command -v pactl >/dev/null 2>&1 || say "no sound: install pulseaudio-utils (pactl, parec) for page audio"
[ $VISIBLE = 1 ] && export ASB_VISIBLE=1
# run "$@" and remove the tmp link (and the pid file) when it ends; the pid file holds the exact PID of "$@"
PIDF="$HCONF/hostd.pid"; mkdir -p "$HCONF" && chmod 700 "$HCONF" 2>/dev/null
supervise_fg(){   # --foreground: this script waits; INT/TERM/HUP go to that one PID (amikit-with-browser.sh stops us)
  "$@" </dev/null >>"$LOG" 2>&1 & _p=$!; echo "$_p" >"$PIDF"
  trap 'kill "$_p" 2>/dev/null' INT TERM HUP
  while kill -0 "$_p" 2>/dev/null; do wait "$_p"; _r=$?; done
  rm -f "$PIDF"; asb_droplink; exit ${_r:-0}; }
supervise_bg(){   # detached (own session): a small sh waits for the program, then removes the link
  export PIDF; _sup='"$@" </dev/null & p=$!; echo "$p" >"$PIDF"; wait "$p"; rm -f "$PIDF"; [ -n "$ASB_TLINK" ] && [ -L "$ASB_TLINK" ] && rm -f "$ASB_TLINK"'
  rm -f "$PIDF"
  if command -v setsid >/dev/null 2>&1; then setsid sh -c "$_sup" sh "$@" </dev/null >>"$LOG" 2>&1 &
  else nohup sh -c "$_sup" sh "$@" </dev/null >>"$LOG" 2>&1 & fi
  _i=0; while [ $_i -lt 20 ] && [ ! -s "$PIDF" ]; do sleep 0.1; _i=$((_i+1)); done; }
if [ $LEGACY = 1 ]; then
  set -- "$HERE/engine.sh" -port "$PORT" -idle "$IDLE"
  echo "=== $(date) start (legacy, plain ASBC) port $PORT idle $IDLE" >>"$LOG"
  if [ $FG = 1 ]; then supervise_fg "$@"; fi
  supervise_bg "$@"
  say "AmiSurf engine (legacy) starting on 127.0.0.1:$PORT (PID $(cat "$PIDF" 2>/dev/null), log $LOG)"; exit 0
fi
# the Host's settings: rewritten on every start (edit run-host.sh options, not this file)
LANV=no; [ $LAN = 1 ] && LANV=yes
cat >"$HCONF/host.conf" <<C
# written by AmiSurf/Host/run-host.sh on every start - changes here are overwritten
name AmiKit ($(uname -n 2>/dev/null || echo PC))
lan $LANV
port $PORT
discovery $LANV
update_check no
data_dir $STATE/services
bundled_browser no
service browser
proto 2
exec sh "$HERE/engine.sh" -port {port} -token {token} -cdp {port2} -idle $IDLE
handshake asbc2
ready_timeout 30
log browser.log
C
set -- "$HOSTD" run --confdir "$HCONF" --config "$HCONF/host.conf" --data-dir "$STATE/services" --no-update-check --exit-idle "$IDLE"
[ $LAN = 1 ] && set -- "$@" --lan || set -- "$@" --listen 127.0.0.1 --no-discovery
echo "=== $(date) start AmiStudio Host port $PORT idle $IDLE lan $LANV ($("$HOSTD" --help 2>&1 | head -1 | cut -d' ' -f1-3), engine: $ENGLAYOUT, HOME $HOME, TMPDIR $TMPDIR, downloads $ASB_DOWNLOADS)" >>"$LOG"
if [ $FG = 1 ]; then supervise_fg "$@"; fi
# detached; the Host stops itself after IDLE minutes without a client (--exit-idle, Host 0.1.3+)
supervise_bg "$@"
say "AmiStudio Host starting on 127.0.0.1:$PORT for AmiSurf (log $LOG; engine log $HCONF/browser.log)"
