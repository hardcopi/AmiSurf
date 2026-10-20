#!/bin/bash
# run68.sh "<AmigaDOS cmd>" [secs] — starts its own FS-UAE (DISPLAY=$ASB_DISPLAY, default :77) and stops only that PID.
E=$(cd "$(dirname "$0")" && pwd); A=${ASB_HD:-$E/hd/System}; S=${2:-120}
DISP=${ASB_DISPLAY:-:77}
pgrep -f "Xvfb $DISP " >/dev/null || { (nohup Xvfb $DISP -screen 0 1920x1080x24 +extension GLX -nolisten tcp >/tmp/asb-xvfb${DISP#:}.log 2>&1 &); sleep 2; }
rm -f $A/ASB/o.done
printf "FailAt 30\n%s\nEcho >SYS:ASB/o.done done\n" "$1" > $A/S/asp-launch
C=${ASB_CFG:-asb}; mkdir -p $E/local
# configs are templates: point ASB_KICKSTART / ASB_DISKS / ASB_HD at YOUR files (ROMs/OS are never in the repo)
sed "s#@KICKSTART@#${ASB_KICKSTART:-/workspace/amiga/kickstarts/kick31-a1200.rom}#; s#@DISKS@#${ASB_DISKS:-/workspace/amiga/disks}#; s#@HD@#${ASB_HD:-$A}#" $E/$C.fs-uae.in > $E/local/$C.fs-uae
cd $E && { DISPLAY=$DISP timeout $((S+15)) fs-uae local/$C.fs-uae >/tmp/asb-uae.log 2>&1 & UPID=$!; }
echo "fs-uae (timeout wrapper) pid $UPID"
for i in $(seq 1 $S); do [ -f $A/ASB/o.done ] && break; sleep 1; done
sleep 1
kill $UPID 2>/dev/null; sleep 1   # only the emulator this script started
printf 'Echo "idle"\n' > $A/S/asp-launch
[ -f $A/ASB/o.done ] && echo "done in ${i}s" || echo "TIMEOUT"
