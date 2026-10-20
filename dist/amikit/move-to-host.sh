#!/bin/sh
# move-to-host.sh - copy this AmiKit edition's browser data (Host/data/config: history, bookmarks, settings, passwords;
# Host/data/profile: the Chromium profile with cookies and logins) to another AmiStudio Host:
#   sh move-to-host.sh [--dry-run]                    the standalone Linux Host 0.1.2+ (installed by hand, systemd):
#        ~/.local/share/amistudio-host/browser and ~/.config/amistudio-host/tokens (ASB_STATE / AMISTUDIO_HOST_DIR
#        override, as for that Host)
#   sh move-to-host.sh --to <AmiStudio>/Host [--dry-run]   the AmiStudio launcher's Host (0.1.8, self-contained):
#        <AmiStudio>/Host/data/services/browser and <AmiStudio>/Host/data/hostd/tokens, inside that drawer
#   --dry-run just says what it would do. It copies only into an empty or missing target folder (never overwrites).
# The bundled Host's pairing tokens (Host/data/hostd/tokens) are copied only if no tokens file is there yet, so paired
# Amigas stay paired. host.conf and the logs are not copied.
# AMIKIT_STATE=/dir: copy from there instead of this edition's data folder.
# The files here are copied, not moved: the AmiKit edition keeps working with them. Stop AmiSurf and both Hosts first.
HERE=$(cd "$(dirname "$0")" && pwd); DRY=0; TO=""
while [ $# -gt 0 ]; do case "$1" in --dry-run|-n) DRY=1 ;; --to) TO=$2; shift ;; -h|--help) sed -n '2,13p' "$0"; exit 0 ;; *) echo "unknown option $1"; exit 2 ;; esac; shift; done
SAVED_STATE="${ASB_STATE:-}"; unset ASB_STATE
. "$HERE/asb-state.sh" 2>/dev/null   # STATE = where this edition keeps its data (Host/data, or the old fallback); ASB_USER_HOME
# 0.86: the standalone Host's folders are in the real home, also when this runs with HOME/XDG_* pointed into Host/data
_uxd=${XDG_DATA_HOME:-}; case "$_uxd" in ""|"$HERE"/*) _uxd="$ASB_USER_HOME/.local/share" ;; esac
_uxc=${XDG_CONFIG_HOME:-}; case "$_uxc" in ""|"$HERE"/*) _uxc="$ASB_USER_HOME/.config" ;; esac
if [ -n "$TO" ]; then
  [ -d "$TO" ] || { echo "no folder $TO (give the AmiStudio drawer's Host folder, e.g. --to ~/AmiKit/Work/AmiStudio/Host)"; exit 1; }
  TO=$(cd "$TO" && pwd); [ "$TO" = "$HERE" ] && { echo "--to $TO is this edition's own Host folder"; exit 1; }
  DST="$TO/data/services/browser"; HCD="$TO/data/hostd"
else
  DST="${SAVED_STATE:-$_uxd/amistudio-host/browser}"   # the Host's ASB_STATE
  HCD="${AMISTUDIO_HOST_DIR:-$_uxc/amistudio-host}"
fi
SRC="${AMIKIT_STATE:-$STATE}"; [ -n "$SAVED_STATE" ] && export ASB_STATE="$SAVED_STATE"
[ "$(cd "$SRC" 2>/dev/null && pwd)" = "$(mkdir -p "$DST" 2>/dev/null; cd "$DST" 2>/dev/null && pwd)" ] && { echo "source and target are the same folder ($DST)"; exit 1; }
# a running engine holds the Chromium profile open: SingletonLock -> "<host>-<pid>". Copying it then gives a broken
# profile. A lock left by an engine that was stopped hard (pid gone on this PC) is stale and fine to copy.
in_use(){ l=$(readlink "$1/profile/SingletonLock" 2>/dev/null) || return 1
  h=${l%-*}; p=${l##*-}; [ "$h" = "$(uname -n)" ] || return 0; kill -0 "$p" 2>/dev/null; }
if in_use "$SRC" || in_use "$DST"; then
  echo "A browser engine is running (profile in use). Quit AmiSurf, stop the Hosts (or wait until they stop) and try again."; exit 1; fi
empty(){ [ ! -e "$1" ] || { [ -d "$1" ] && [ -z "$(ls -A "$1" 2>/dev/null)" ]; }; }
n=0; skip=0
for x in config profile; do
  [ -d "$SRC/$x" ] && [ -n "$(ls -A "$SRC/$x" 2>/dev/null)" ] || { echo "nothing to copy: $SRC/$x is empty or missing"; continue; }
  if ! empty "$DST/$x"; then echo "kept: $DST/$x already has data - not touched (move it away first to copy over it)"; skip=$((skip+1)); continue; fi
  if [ $DRY = 1 ]; then echo "would copy $SRC/$x -> $DST/$x"; n=$((n+1)); continue; fi
  mkdir -p "$DST" && rmdir "$DST/$x" 2>/dev/null; cp -a "$SRC/$x" "$DST/$x" && { echo "copied $SRC/$x -> $DST/$x"; n=$((n+1)); } || { echo "copy of $x failed (partial copy left in $DST/$x)"; exit 1; }
done
if [ -s "$SRC/hostd/tokens" ]; then
  if [ -e "$HCD/tokens" ]; then echo "kept: $HCD/tokens already exists - not touched (pair those Amigas again if needed)"; skip=$((skip+1))
  elif [ $DRY = 1 ]; then echo "would copy $SRC/hostd/tokens -> $HCD/tokens"; n=$((n+1))
  else mkdir -p "$HCD" && chmod 700 "$HCD" && cp -p "$SRC/hostd/tokens" "$HCD/tokens" && chmod 600 "$HCD/tokens" && { echo "copied $SRC/hostd/tokens -> $HCD/tokens (paired Amigas stay paired)"; n=$((n+1)); } || { echo "copy of tokens failed"; exit 1; }; fi
fi
[ -d "$SRC/WidevineCdm" ] && echo "note: Widevine (DRM video) is not copied; add it for the Host its own way (it is Google's, not ours)"
[ $DRY = 1 ] && { echo "dry run: $n item(s) would be copied, $skip kept"; exit 0; }
echo "done: $n item(s) copied, $skip kept. Downloads stay in ~/Downloads."
[ $n -gt 0 ] && echo "$(date '+%F %T') copied to $DST by move-to-host.sh" >>"$SRC/MOVED-TO-HOST.txt"
exit 0
