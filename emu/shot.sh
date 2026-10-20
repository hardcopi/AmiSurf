#!/bin/bash
# shot.sh out.png delay_s "<AmigaDOS cmd>" [secs] — run our emu on :77 and grab the screen after delay
OUT=$1; D=$2; CMD=$3; S=${4:-60}
XD=${ASB_DISPLAY:-:77}; pgrep -f "Xvfb $XD " >/dev/null || { (nohup Xvfb $XD -screen 0 1920x1080x24 +extension GLX -nolisten tcp >/tmp/asb-xvfb${XD#:}.log 2>&1 &); sleep 2; }
( sleep $D; ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i $XD -frames:v 1 "$OUT" ) &
"$(dirname "$0")"/run68.sh "$CMD" $S
wait
