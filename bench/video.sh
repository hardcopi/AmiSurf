#!/bin/bash
# video.sh TREE MODE BACKEND LABEL : Big Buck Bunny (official Blender upload) fps. MODE linux|040|020.
# Plays 45 s (start and any ads), then measures 30 s. Kills only the PIDs it started.
T=$1; MODE=$2; B=$3; OUT=/tmp/bench/$4; mkdir -p $OUT; P=9293
URL="https://www.youtube.com/watch?v=aqz-KE-bpKQ"
rm -rf /tmp/bench/vprof* /tmp/bench/vcfg
ASB_CONFIG=/tmp/bench/vcfg ASB_CHROME_DISPLAY=${CDISP:-:95} $T/build/host/asb-chromed -browser $B -port $P -cdp $((P-1)) -profile /tmp/bench/vprof about:blank >$OUT/host.log 2>&1 & HP=$!
sleep 8
if [ $MODE = linux ]; then
  PROBE_FMT=2 ASB_FRAMES=1 timeout 120 python3 /tmp/bench/probe.py $P 'wait 1' "nav $URL" 'wait 45' 'mark scroll 0' 'wait 30' 'mark end 0' >$OUT/probe.log 2>&1
else
  CFG=asb; BIN=AmiSurf; [ $MODE = 020 ] && { CFG=asb020fpu; BIN=AmiSurf-020; }
  cp $T/build/amiga/$BIN /tmp/hdf68/ASB/AmiSurf
  cd /workspace/amistudio-browser
  ASB_CFG=$CFG ASB_HD=/tmp/hdf68 ASB_DISPLAY=:93 ASB_DISKS=/tmp/nodisks ASB_KICKSTART=/workspace/amiga/kickstarts/kick31-a1200.rom setsid nohup emu/run68.sh "cd SYS:ASB
AmiSurf -port $P THEME=PRO STATS $URL" 600 >$OUT/emu.log 2>&1 & RP=$!
  sleep 4; UP=$(grep -o 'pid [0-9]*' $OUT/emu.log | grep -o '[0-9]*')
  for k in $(seq 150); do grep -q "client connected" $OUT/host.log && break; sleep 1; done
  sleep 45; echo "S $(wc -l <$OUT/host.log)" >$OUT/marks
  sleep 30; echo "E $(wc -l <$OUT/host.log)" >>$OUT/marks
  ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i :93 -frames:v 1 $OUT/shot.png
  kill $UP $RP
fi
kill $HP; sleep 3; echo done
