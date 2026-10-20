#!/bin/bash
# test68.sh HOSTLOG [DISPLAY] — drive a running 68k client in FS-UAE (started with emu/run68.sh against a host
# that logs to HOSTLOG) through the address bar, reload, find, zoom and About, and check what reached the host.
# Default: the MUI client (AmiSurf / -020) on a 1920x1080 RTG screen, default MUI prefs, topaz screen font;
# it also checks the MUI tab strip (+ button), a MUI menustrip shortcut and the find bar's close button.
# GADTOOLS=1: the AmiSurf-gadtools fallback with TOOLBAR=LARGE (the 0.68/0.69 layout).
# SLOW=N stretches the waits (020 at real speed: SLOW=4).
# NOCLICK=1: keyboard checks only (other screen layouts, e.g. the 020 on AGA); TYPEDELAY=ms for slow machines.
# Needs xdotool + ffmpeg. Amiga key = Super_R. Screens go to /tmp/test68-*.png.  Only sends input; kills nothing.
L=${1:?host log}; export DISPLAY=${2:-:93}; fails=0
K(){ xdotool keydown $1; sleep .15; xdotool keyup $1; sleep .15; }
KC(){ xdotool keydown $1; sleep .1; xdotool keydown $2; sleep .2; xdotool keyup $2; sleep .1; xdotool keyup $1; sleep .2; }
S(){ ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i $DISPLAY -frames:v 1 "$1"; }
sl(){ sleep $(awk "BEGIN{print $1*${SLOW:-1}}"); }   # SLOW=4 on a real-speed 020
mark(){ wc -l <"$L"; }
saw(){ tail -n +$(( $1 + 1 )) "$L" | grep -qE "$2"; }
chk(){ if saw "$1" "$2"; then echo "ok   $3"; else echo "FAIL $3"; fails=$((fails+1)); fi; }
xdotool mousemove 960 600; sleep .3
U="https://example.com/"
m=$(mark); KC Super_R l; sleep .5; xdotool type --delay ${TYPEDELAY:-120} "$U"; K Return; sl 8
chk $m "op 2 n=${#U}\$" "address bar: Amiga+L, type URL, Return sends NAV"
m=$(mark); KC Super_R r; sl 3; chk $m "op 5 n=0" "reload (Amiga+R)"
m=$(mark); KC Super_R f; sleep .5; xdotool type --delay ${TYPEDELAY:-120} "Example"; K Return; sl 3
chk $m "op 13 n=[1-9]" "find (Amiga+F, query, Return)"; K Escape; sl 1
m=$(mark); KC Super_R equal; sl 2; KC Super_R 0; sl 2
chk $m "cmd zoom \\+" "zoom in (Amiga+=)"; chk $m "cmd zoom 0" "zoom reset (Amiga+0)"
C(){ xdotool mousemove $1 $2; sleep .35; xdotool mousedown 1; sleep .25; xdotool mouseup 1; sleep .3; }
if [ "$GADTOOLS" = 1 ]; then MENU="1894 46"; ABOUT="1700 512"; else MENU="${MENU_XY:-1901 35}"; ABOUT="${ABOUT_XY:-1700 411}"; fi
if [ "$GADTOOLS" != 1 ]; then
  if [ "$NOCLICK" = 1 ]; then m=$(mark); KC Super_R t; sl 4; chk $m "op 15 n=0" "new tab (Amiga+T)"
  else m=$(mark); C ${PLUS_XY:-553 8}; sl 4; chk $m "op 15 n=0" "MUI tab strip: + opens a tab"; fi
  m=$(mark); KC Super_R w; sl 3; chk $m "op 16 n=1" "close tab (Amiga+W, MUI menustrip shortcut)"
  m=$(mark); KC Super_R d; sl 2; chk $m "cmd bookmark|op 18 n=8" "bookmark (Amiga+D, MUI menustrip shortcut)"
  m=$(mark); KC Super_R f; sl 2; S /tmp/test68-find.png
  if [ "$NOCLICK" = 1 ]; then K Escape; else C ${FINDX_XY:-1903 1053}; fi; sl 3; S /tmp/test68-nofind.png
  if cmp -s /tmp/test68-find.png /tmp/test68-nofind.png; then echo "FAIL MUI find bar: did not close"; fails=$((fails+1)); else echo "ok   MUI find bar opens (Amiga+F) and closes ($([ "$NOCLICK" = 1 ] && echo Esc || echo x))"; fi
  chk $m "cmd findclose|op 18 n=9" "find bar close tells the host (findclose)"
fi
if [ "$NOCLICK" = 1 ]; then echo "$([ $fails = 0 ] && echo 'all passed' || echo FAILED) ($fails failed, NOCLICK)"; exit $((fails>0)); fi
S /tmp/test68-before-about.png; C $MENU; sl 6; C $ABOUT; sl 6; S /tmp/test68-about.png; K Escape; K Return; sl 2
if cmp -s /tmp/test68-before-about.png /tmp/test68-about.png; then echo "FAIL About (menu) opened nothing"; fails=$((fails+1)); else echo "ok   About window (menu > About) shown"; fi
echo "$([ $fails = 0 ] && echo 'all passed' || echo FAILED) ($fails failed)"; exit $((fails>0))
