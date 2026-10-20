#!/bin/sh
# engine.sh - the AmiSurf browser engine (asb-chromed + Chromium, headless). The bundled AmiStudio Host runs it
# (host.conf written by run-host.sh: engine.sh -port {port} -token {token} -cdp {port2} -idle N); run-host.sh --legacy
# runs it directly on 9223. Extra arguments go to asb-chromed. Data: this edition's Host/data (asb-state.sh); downloads:
# the real ~/Downloads (ASB_DOWNLOADS, resolved from ASB_USER_HOME, not the Host/data/home that HOME points to).
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/asb-state.sh"
asb_selfcontained   # 0.86: HOME, XDG_*, TMPDIR under Host/data (already so when the Host runs us); downloads: real ~/Downloads
if [ $ENGLAYOUT = host ]; then
  # the AmiStudio Host release's own engine script, with this edition's data folder; its downloads default is
  # $HOME/Downloads, so ASB_DOWNLOADS (the real ~/Downloads) is passed on explicitly
  export ASB_STATE="$STATE" ASB_DOWNLOADS; exec sh "$ENG/engine.sh" "$@"
fi
# own engine copy in Host/ (make AMIKIT_CHROMIUM=own)
# Widevine (DRM video) only if the user added it with get-widevine.sh (Google's, not shipped), else Google Chrome's
WV="$STATE/WidevineCdm"; [ -f "$WV/current.txt" ] && export ASB_WIDEVINE_DIR="$WV/$(cat "$WV/current.txt")"
export ASB_CHROME="$CHROME" ASB_CONFIG="$STATE/config" ASB_DOWNLOADS="${ASB_DOWNLOADS:-$ASB_USER_HOME/Downloads}"
if [ "${ASB_VISIBLE:-0}" = 1 ] && [ -n "${DISPLAY:-}" ]; then export ASB_CHROME_DISPLAY="$DISPLAY"; else export ASB_HEADLESS=1; fi
unset ASB_TOKEN
# Chrome's private debug port: the Host passes a free one (-cdp {port2}); otherwise the first free one from 9323
case " $* " in *" -cdp "*) CDPARG="" ;; *)
  CDP=9323; if command -v nc >/dev/null 2>&1; then while nc -z 127.0.0.1 $CDP 2>/dev/null; do CDP=$((CDP+1)); done; fi
  CDPARG="-cdp $CDP" ;; esac
exec "$ENG/asb-chromed" -browser chrome $CDPARG -profile "$STATE/profile" "$@"
