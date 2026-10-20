#!/bin/bash
# bench.sh — run the layout benchmark (test/bench.c) in OUR emu; output in emu/hd/System/ASB/bench-out.txt
cd "$(dirname "$0")/.." && make -s build/amiga/bench build/amiga/bench040 2>&1 | grep -i error
A=emu/hd/System/ASB; cp build/amiga/bench build/amiga/bench040 $A/; mkdir -p $A/wikicss; cp test/pages/wikicss/* $A/wikicss/; cp test/pages/wiki-amiga.html $A/wiki.html
emu/run68.sh "cd SYS:ASB
Stack 200000
bench wiki.html https://en.wikipedia.org/wiki/Amiga wikicss 1888 3 85 >bench-out.txt
bench040 wiki.html https://en.wikipedia.org/wiki/Amiga wikicss 1888 2 85 >>bench-out.txt" 300 >/dev/null
cat $A/bench-out.txt
