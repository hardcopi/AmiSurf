#!/bin/bash
# copy the working tree to /tmp/new69, add the temporary benchmark instrumentation, build
rsync -a --delete --exclude .git --exclude shots --exclude dist /workspace/amistudio-browser/ /tmp/new69/
cd /tmp/new69 && python3 /tmp/bench/instr.py ui/asb_gfx_chrome.c && make build/amiga/AmiSurf build/host/asb-chromed 2>&1 | rg -i "error|warn"; ls -la build/amiga/AmiSurf
