# asb-state.sh - sourced by run-host.sh, engine.sh, amikit-with-browser.sh ... (needs HERE = the Host drawer).
# Picks the engine's state folder and sets STATE and LOG (and HOSTD, ENG, CHROME, ENGLAYOUT, see the end):
#   STATE/config   history.tsv, bookmarks.tsv, settings, passwords
#   STATE/profile  the Chromium profile (cookies, cache, logins)
#   STATE/hostd    the bundled Host's --confdir (host.conf, tokens, hostd.log, browser.log)
#   STATE/services the Host's data_dir ({state}: e.g. the bundled AmiDiscord service's profile)
#   STATE/home     HOME and the XDG config/data/state/cache folders for Chromium, fontconfig, NSS ... (0.86)
#   STATE/tmp      TMPDIR (Chromium's SingletonSocket; a short /tmp link when the path is too long, see asb_selfcontained)
#   STATE/host.log the run-host.sh / engine log
# Default (0.77): AmiSurf/Host/data, inside the drawer, so it moves and backs up with it (0.86: AmiStudio suite rule
# "self-contained install folder": nothing in ~/.config, ~/.local, ~/.cache, ~/.pki or /tmp).
# ASB_STATE=/some/dir overrides it. If the drawer cannot hold it (read-only, noexec, no symlinks for
# Chromium's profile lock) the old ~/.local/state/amisurf-amikit is used and the reason logged.
# Downloads are not here: they stay in the real ~/Downloads (Rich: "Except downloads is fine"; ASB_DOWNLOADS
# overrides). ASB_USER_HOME is that real home, found before HOME is pointed into STATE/home.
if [ -z "${ASB_USER_HOME:-}" ]; then
  case "${HOME:-}" in
    ""|"$HERE"|"$HERE"/*) ASB_USER_HOME=$(getent passwd "$(id -u)" 2>/dev/null | cut -d: -f6) ;;   # HOME is already ours
    *) ASB_USER_HOME=$HOME ;;
  esac
  [ -n "$ASB_USER_HOME" ] && [ -d "$ASB_USER_HOME" ] || ASB_USER_HOME=${HOME:-/tmp}
fi
export ASB_USER_HOME
_asb_uxs=${XDG_STATE_HOME:-}; case "$_asb_uxs" in ""|"$HERE"/*) _asb_uxs="$ASB_USER_HOME/.local/state" ;; esac
OLDSTATE="$_asb_uxs/amisurf-amikit"
_asb_msgs=""
_asb_note(){ _asb_msgs="${_asb_msgs}state: $*
"; }
_asb_usable(){   # writable, can run a script, can make symlinks (Chromium's SingletonLock is a symlink)
  d="$1"; mkdir -p "$d" 2>/dev/null || { _asb_why="cannot create $d"; return 1; }
  t="$d/.asb-test-$$"
  printf '#!/bin/sh\nexit 0\n' >"$t" 2>/dev/null || { _asb_why="$d is not writable"; return 1; }
  chmod +x "$t" 2>/dev/null; if ! "$t" 2>/dev/null; then rm -f "$t"; _asb_why="$d is on a noexec drive"; return 1; fi
  if ! ln -s "$t" "$t.l" 2>/dev/null; then rm -f "$t"; _asb_why="$d does not support symlinks (Chromium needs them)"; return 1; fi
  rm -f "$t" "$t.l"; return 0; }
_asb_empty(){ [ ! -d "$1" ] || [ -z "$(ls -A "$1" 2>/dev/null)" ]; }
if [ -n "${ASB_STATE:-}" ]; then
  STATE="$ASB_STATE"; mkdir -p "$STATE"; _asb_note "using ASB_STATE=$STATE"
else
  NEWSTATE="$HERE/data"; _asb_wasempty=0; _asb_empty "$NEWSTATE" && _asb_wasempty=1
  if _asb_usable "$NEWSTATE"; then
    STATE="$NEWSTATE"
    if [ $_asb_wasempty = 1 ] && [ -d "$OLDSTATE" ] && ! _asb_empty "$OLDSTATE"; then
      for x in config profile host.log; do
        [ -e "$OLDSTATE/$x" ] || continue
        if mv "$OLDSTATE/$x" "$STATE/" 2>/dev/null; then _asb_note "moved $OLDSTATE/$x to $STATE/"
        elif cp -a "$OLDSTATE/$x" "$STATE/" 2>/dev/null; then rm -rf "$OLDSTATE/$x"; _asb_note "copied $OLDSTATE/$x to $STATE/ (and removed the old copy)"
        else _asb_note "could not move $OLDSTATE/$x to $STATE/ - left it where it was"; fi
      done
      printf 'AmiSurf 0.77 moved its history, bookmarks, settings, passwords, Chromium profile and\nhost.log from this folder into the AmiSurf drawer:\n  %s\nDownloads still go to ~/Downloads. This folder can be deleted.\n' "$STATE" >"$OLDSTATE/MOVED-TO-DRAWER.txt" 2>/dev/null
    fi
  else
    STATE="$OLDSTATE"; mkdir -p "$STATE"; _asb_note "cannot keep state in the drawer ($_asb_why); using $STATE"
  fi
fi
mkdir -p "$STATE/config" 2>/dev/null
LOG="$STATE/host.log"
[ -n "$_asb_msgs" ] && printf '%s' "$_asb_msgs" | sed "s/^/=== $(date '+%F %T') /" >>"$LOG"

# Where Host and engine are (0.83). Two layouts, chosen when the bundle is built (make AMIKIT_CHROMIUM=host|own) and
# named in Host/LAYOUT (0.86):
#   host (0.86):    Host/amistudio-host/ is the unpacked AmiStudio Host release (amistudio-hostd, engine/engine.sh,
#                   engine/asb-chromed, engine/chromium) - one Chromium, used as it is
#   own (0.85):     Host/amistudio-hostd + Host/asb-chromed + Host/chromium (the edition's own engine copy)
# Host/LAYOUT wins when the files of that layout are there: 0.86 (host) unpacked over 0.85 (own) leaves 0.85's
# Host/amistudio-hostd, asb-chromed and chromium/ behind, which must not be picked (they can be deleted).
# Without Host/LAYOUT (0.85 and older): own wins when its files are there.
_asb_lay=$(sed -n '1s/[^a-z].*//p' "$HERE/LAYOUT" 2>/dev/null)
_asb_hasown=0; [ -f "$HERE/amistudio-hostd" ] && [ -f "$HERE/asb-chromed" ] && [ -f "$HERE/chromium/chrome" ] && _asb_hasown=1
_asb_hashost=0; [ -f "$HERE/amistudio-host/amistudio-hostd" ] && [ -f "$HERE/amistudio-host/engine/engine.sh" ] && _asb_hashost=1
if [ "$_asb_lay" = host ] && [ $_asb_hashost = 1 ]; then ENGLAYOUT=host
elif [ "$_asb_lay" = own ] && [ $_asb_hasown = 1 ]; then ENGLAYOUT=own
elif [ $_asb_hasown = 1 ] || [ $_asb_hashost = 0 ]; then ENGLAYOUT=own
else ENGLAYOUT=host; fi
if [ $ENGLAYOUT = own ]; then HOSTD="$HERE/amistudio-hostd"; ENG="$HERE"; else HOSTD="$HERE/amistudio-host/amistudio-hostd"; ENG="$HERE/amistudio-host/engine"; fi
CHROME="$ENG/chromium/chrome"

# 0.86 self-contained (same as the AmiStudio launcher's Host/run-host.sh): HOME, XDG_*, TMPDIR under STATE; downloads
# in the real ~/Downloads. Sets ASB_TLINK when TMPDIR is a short /tmp link (asb_droplink removes it).
asb_selfcontained(){
  _h="$STATE/home"
  mkdir -p "$_h/.config" "$_h/.local/share" "$_h/.local/state" "$_h/.cache" "$STATE/tmp" "$STATE/services" 2>/dev/null
  chmod 700 "$_h" "$STATE/tmp" "$STATE/services" 2>/dev/null
  export ASB_DOWNLOADS="${ASB_DOWNLOADS:-$ASB_USER_HOME/Downloads}"
  # page sound: Chromium and pactl/parec find the user's PulseAudio/PipeWire through HOME and XDG_RUNTIME_DIR, which
  # change below; name the running server (and its cookie) so they reach it and do not start a second one in here
  if [ -z "${PULSE_SERVER:-}" ] && command -v pactl >/dev/null 2>&1; then
    _ps=$(pactl info 2>/dev/null | sed -n 's/^Server String: //p' | head -n 1)
    case "$_ps" in /*) export PULSE_SERVER="unix:$_ps" ;; unix:*|tcp:*) export PULSE_SERVER="$_ps" ;; esac
  fi
  if [ -z "${PULSE_COOKIE:-}" ]; then
    for _c in "${XDG_CONFIG_HOME:-$ASB_USER_HOME/.config}/pulse/cookie" "$ASB_USER_HOME/.config/pulse/cookie" "$ASB_USER_HOME/.pulse-cookie"; do
      case "$_c" in "$STATE"/*) continue ;; esac
      [ -f "$_c" ] && { export PULSE_COOKIE="$_c"; break; }
    done
  fi
  # Chromium keeps a Unix socket in TMPDIR (org.chromium.Chromium.XXXXXX/SingletonSocket) and a socket path has at
  # most 107 bytes: in a deep drawer TMPDIR is a short link /tmp/amistudio-host-<uid>-<hash> to STATE/tmp (the files
  # stay in the drawer; the link goes when the Host stops)
  _td="$STATE/tmp"; ASB_TLINK=""
  if [ ${#_td} -gt 56 ]; then
    _l=/tmp/amistudio-host-$(id -u)-$(printf '%s' "$STATE" | cksum | cut -d' ' -f1)
    if { [ -L "$_l" ] || [ ! -e "$_l" ]; } && ln -sfn "$_td" "$_l" 2>/dev/null; then _td=$_l; ASB_TLINK=$_l; fi
  fi
  export HOME="$_h" XDG_CONFIG_HOME="$_h/.config" XDG_DATA_HOME="$_h/.local/share" XDG_STATE_HOME="$_h/.local/state" \
    XDG_CACHE_HOME="$_h/.cache" TMPDIR="$_td" ASB_TLINK
  unset XDG_RUNTIME_DIR
}
asb_droplink(){ [ -n "${ASB_TLINK:-}" ] && [ -L "$ASB_TLINK" ] && rm -f "$ASB_TLINK"; return 0; }
