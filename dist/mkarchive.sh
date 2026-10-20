#!/bin/sh
# mkarchive.sh [version] [out.lha]: stage and pack the release with jlha (AmiStudio Paint's dist/mkarchive.sh pattern).
# LHA header level 1 (MS-DOS local time stamps, shown correctly on the Amiga). .info icons are copied byte-for-byte.
R=$(cd "$(dirname "$0")/.." && pwd)
V=${1:-$(sed -n 's/^#define ASB_VERSION "\(.*\)"/\1/p' $R/ui/asb_gfx_chrome.c)}
S=/tmp/lha-asb-$V; OUT=${2:-$R/dist/AmiSurf-$V.lha}; A=$S/AmiSurf
rm -rf $S && mkdir -p $A/Icons || exit 1
for b in AmiSurf AmiSurf-020 AmiSurf-gadtools; do cp -p $R/build/amiga/$b $A/ || exit 1; done
cp -p $R/dist/docs/README $A/ReadMe && cp -p $R/dist/docs/CHANGES $R/dist/docs/ATTRIBUTIONS $A/ || exit 1
cp -p $R/assets/about-banner-512.png $A/about-banner.png && cp -p $R/ui/icons/*.png $A/Icons/ || exit 1
sh $R/tools/licenses.sh $A/LICENSES 2>/dev/null || bash $R/tools/licenses.sh $A/LICENSES || exit 1
cp -p $R/dist/info/drawer.info $S/AmiSurf.info || exit 1
for b in AmiSurf AmiSurf-020 AmiSurf-gadtools; do cp -p $R/dist/info/AmiSurf.info $A/$b.info || exit 1; done
cp -p $R/dist/info/README.info $A/ReadMe.info; for f in CHANGES ATTRIBUTIONS LICENSES; do cp -p $R/dist/info/$f.info $A/; done
for b in AmiSurf AmiSurf-020 AmiSurf-gadtools; do grep -q "AmiSurf $V" $A/$b || { echo "$b is not $V"; exit 1; }; done
# ASB_HOSTDIR=<bundle>/AmiSurf/Host: the AmiKit edition, Linux engine inside the drawer (exec bits are
# restored by run-host.sh; level 1 headers still carry the Unix mode in jlha's extended header)
if [ -n "$ASB_HOSTDIR" ]; then cp -a "$ASB_HOSTDIR" $A/Host || exit 1; fi
rm -f $OUT; (cd $S && jlha c1q "$OUT" AmiSurf.info AmiSurf) >/dev/null 2>&1
[ -s "$OUT" ] || { echo "jlha failed"; exit 1; }
echo "$OUT $(stat -c %s $OUT) $(md5sum < $OUT | cut -c1-32)"
