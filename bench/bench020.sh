#!/bin/bash
# bench020.sh TREE CFG LABEL : like bench68k.sh but restarts the client per page with the URL as argument
# (typing into the address bar loses keys at real 020 speed). Kills only the PIDs it started.
T=$1; CFG=$2; LB=$3; P=9263; OUT=/tmp/bench/$LB; mkdir -p $OUT; rm -f $OUT/marks
PAGES=("https://www.google.com/search?q=amiga+1200" "https://www.youtube.com/" "https://www.bbc.com/news" "https://github.com/torvalds/linux" "https://news.ycombinator.com/")
rm -rf /tmp/bench/prof /tmp/bench/cfg
ASB_CONFIG=/tmp/bench/cfg ASB_CHROME_DISPLAY=${CDISP:-:92} $T/build/host/asb-chromed ${HOSTARGS:-} -port $P -cdp $((P-1)) -profile /tmp/bench/prof about:blank >$OUT/host.log 2>&1 & HP=$!
cp $T/build/amiga/${BIN:-AmiSurf} /tmp/hdf68/ASB/AmiSurf; cp $T/ui/icons/*.png /tmp/hdf68/ASB/Icons/
export DISPLAY=:93
K(){ xdotool keydown $1; sleep .15; xdotool keyup $1; sleep .15; }
cd /workspace/amistudio-browser
for i in ${IDX:-0 1 2 3 4}; do
  n0=$(grep -c "client connected" $OUT/host.log)
  ASB_CFG=$CFG ASB_HD=/tmp/hdf68 ASB_DISPLAY=:93 ASB_DISKS=/tmp/nodisks ASB_KICKSTART=/workspace/amiga/kickstarts/kick31-a1200.rom setsid nohup emu/run68.sh "cd SYS:ASB
AmiSurf -port $P THEME=PRO ${EXTRA:-} ${PAGES[$i]}" 900 >$OUT/emu$i.log 2>&1 & RP=$!
  sleep 4; UP=$(grep -o 'pid [0-9]*' $OUT/emu$i.log | grep -o '[0-9]*')
  for k in $(seq 150); do [ $(grep -c "client connected" $OUT/host.log) -gt $n0 ] && break; sleep 1; done
  echo "PAGE $i $(wc -l <$OUT/host.log)" >>$OUT/marks
  sleep ${WAIT:-90}; xdotool mousemove 960 1070
  echo "SCROLL $i $(wc -l <$OUT/host.log)" >>$OUT/marks
  for k in $(seq 25); do K Down; done; sleep ${SETTLE:-30}
  echo "END $i $(wc -l <$OUT/host.log)" >>$OUT/marks
  ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i :93 -frames:v 1 $OUT/page$i.png
  kill $UP $RP; sleep 3
done
kill $HP; echo done
