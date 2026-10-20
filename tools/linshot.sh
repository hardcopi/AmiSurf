#!/bin/bash
# linshot.sh out.png "<asb-sdl args>" — run Linux front end on private Xvfb :78 (1920x1080), grab with ffmpeg once the page has painted
OUT=$1; shift
pgrep -f "Xvfb :78" >/dev/null || { (nohup Xvfb :78 -screen 0 1920x1080x24 -nolisten tcp >/tmp/asb-xvfb78.log 2>&1 &); sleep 2; }
B=/workspace/amistudio-browser/build/host/asb-sdl
rm -f /tmp/asb-sdl.ready
( DISPLAY=:78 $B -shot /tmp/asb-sdl.ready.bmp -quit 4000 "$@" 2>/tmp/asb-sdl.log ) &
P=$!
for i in $(seq 1 120); do [ -f /tmp/asb-sdl.ready.bmp ] && break; sleep 0.5; done
sleep 0.5; ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i :78 -frames:v 1 "$OUT"
wait $P; rm -f /tmp/asb-sdl.ready.bmp; cat /tmp/asb-sdl.log
