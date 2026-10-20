#!/bin/bash
# run.sh NAME BIN MODE(headless|xvfb) [extra flags...]  -> NAME-shot.png NAME-cast.png NAME.out
H=$(cd "$(dirname "$0")" && pwd); W=${DRM_WORK:-/tmp/asb-drm}; mkdir -p $W
N=$1; B=$2; M=$3; shift 3; P=$((9500+RANDOM%400)); D=$W/prof-$N; rm -rf $D; [ -n "$PREP" ] && eval "$PREP"
if [ $M = xvfb ]; then pgrep -f "Xvfb :88 " >/dev/null || { (Xvfb :88 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &); sleep 2; }; export DISPLAY=:88; HL=; else HL=--headless=new; fi
$B $HL --remote-debugging-port=$P --user-data-dir=$D --no-first-run --no-default-browser-check --autoplay-policy=no-user-gesture-required --window-size=1000,700 "$@" about:blank > $W/$N.chrome.log 2>&1 &
CP=$!; sleep 4
node --experimental-websocket $H/cdp2.js $P "${URL:-http://localhost:8779/drm.html}" $W/$N | tee $W/$N.out
kill $CP; sleep 2; kill -0 $CP 2>/dev/null && kill -9 $CP
python3 $H/stat.py $W/$N-shot.png $W/$N-cast.png
