#!/bin/bash
# shots.sh prefix "<AmigaDOS cmd>" secs t1 t2 ... — run OUR emu (:77) and grab the screen at each time t (s)
P=$1; CMD=$2; S=$3; shift 3
D=${ASB_DISPLAY:-:77}; pgrep -f "Xvfb $D " >/dev/null || { (nohup Xvfb $D -screen 0 1920x1080x24 +extension GLX -nolisten tcp >/tmp/asb-xvfb${D#:}.log 2>&1 &); sleep 2; }
for t in "$@"; do ( sleep $t; ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i $D -frames:v 1 "${P}-${t}.png" ) & done
"$(dirname "$0")"/run68.sh "$CMD" $S
wait
