#!/bin/bash
# linshot_at.sh out.png secs "<asb-sdl args>" — like linshot.sh but grabs after a fixed delay (timers / scripts had time to run)
OUT=$1; D=$2; shift 2
pgrep -f "Xvfb :78" >/dev/null || { (nohup Xvfb :78 -screen 0 1920x1080x24 -nolisten tcp >/tmp/asb-xvfb78.log 2>&1 &); sleep 2; }
( DISPLAY=:78 /workspace/amistudio-browser/build/host/asb-sdl -quit $(( (D+3)*1000 )) "$@" 2>/tmp/asb-sdl.log ) & P=$!
sleep $D; ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i :78 -frames:v 1 "$OUT"; wait $P; cat /tmp/asb-sdl.log
