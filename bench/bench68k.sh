#!/bin/bash
# bench68k.sh TREE CFG LABEL : host+client from TREE, FS-UAE config CFG (asb=040, asb020=020) on :93
T=$1; CFG=$2; LB=$3; P=9263; OUT=/tmp/bench/$LB; mkdir -p $OUT; rm -f $OUT/marks
PAGES=("https://www.google.com/search?q=amiga+1200" "https://www.youtube.com/" "https://www.bbc.com/news" "https://github.com/torvalds/linux" "https://news.ycombinator.com/")
rm -rf /tmp/bench/prof /tmp/bench/cfg
ASB_CONFIG=/tmp/bench/cfg ASB_CHROME_DISPLAY=${CDISP:-:92} ASB_DEBUG=${HDBG:-} $T/build/host/asb-chromed ${HOSTARGS:-} -port $P -cdp $((P-1)) -profile /tmp/bench/prof about:blank >$OUT/host.log 2>&1 & HP=$!
echo "host $HP"
cp $T/build/amiga/${BIN:-AmiSurf} /tmp/hdf68/ASB/AmiSurf; cp $T/ui/icons/*.png /tmp/hdf68/ASB/Icons/
cd /workspace/amistudio-browser
ASB_CFG=$CFG ASB_HD=/tmp/hdf68 ASB_DISPLAY=:93 ASB_DISKS=/tmp/nodisks ASB_KICKSTART=/workspace/amiga/kickstarts/kick31-a1200.rom setsid nohup emu/run68.sh "cd SYS:ASB
AmiSurf -port $P THEME=PRO about:blank" 1500 >$OUT/emu.log 2>&1 & RP=$!
echo "run68 $RP"; sleep 4; UP=$(grep -o 'pid [0-9]*' $OUT/emu.log | grep -o '[0-9]*'); echo "fsuae $UP"
export DISPLAY=:93
K(){ xdotool keydown $1; sleep .15; xdotool keyup $1; sleep .15; }
KC(){ xdotool keydown $1; sleep .1; xdotool keydown $2; sleep .2; xdotool keyup $2; sleep .1; xdotool keyup $1; sleep .2; }
for i in $(seq 120); do grep -q "client connected" $OUT/host.log && break; sleep 1; done; sleep ${BOOT:-15}
xdotool mousemove 960 1070
for i in ${IDX:-0 1 2 3 4}; do
  echo "PAGE $i $(wc -l <$OUT/host.log)" >>$OUT/marks
  KC Super_R l; sleep .5; xdotool type --delay 60 "${PAGES[$i]}"; K Return; sleep ${WAIT:-25}
  echo "SCROLL $i $(wc -l <$OUT/host.log)" >>$OUT/marks
  for k in $(seq 25); do K Down; done; sleep ${SETTLE:-0}
  echo "END $i $(wc -l <$OUT/host.log)" >>$OUT/marks
  ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i :93 -frames:v 1 $OUT/page$i.png
  sleep 3
done
kill $UP $RP $HP; sleep 2; echo done
