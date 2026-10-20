#!/bin/bash
# benchlinux.sh TREE LABEL : host from TREE, probe client (1400x770) measuring the frame stream
T=$1; OUT=/tmp/bench/$2; mkdir -p $OUT; P=9273
PAGES=("https://www.google.com/search?q=amiga+1200" "https://www.youtube.com/" "https://www.bbc.com/news" "https://github.com/torvalds/linux" "https://news.ycombinator.com/")
rm -rf /tmp/bench/lprof /tmp/bench/lcfg
ASB_CONFIG=/tmp/bench/lcfg ASB_CHROME_DISPLAY=${CDISP:-:92} $T/build/host/asb-chromed ${HOSTARGS:-} -port $P -cdp $((P-1)) -profile /tmp/bench/lprof about:blank >$OUT/host.log 2>&1 & HP=$!
sleep 6
A=('wait 3')
for i in 0 1 2 3 4; do A+=("mark nav $i" "nav ${PAGES[$i]}" 'wait 25' "mark scroll $i"); for k in $(seq 25); do A+=('key 40' 'wait 0.3'); done; A+=("mark end $i" 'wait 3'); done
PROBE_FMT=${PROBE_FMT:-} ASB_FRAMES=1 python3 /tmp/bench/probe.py $P "${A[@]}" > $OUT/probe.log 2>&1
kill $HP; echo done
