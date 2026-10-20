#!/bin/bash
# mkbundle.sh VERSION CHROMIUM_DIR LICENSE_DIR HOST_TGZ HOST_SHA256 [host|own]
#   assemble build/amisurf-amikit-host-VERSION-linux-x86_64/AmiSurf/Host (+ the .tar.gz in dist/)
# The AmiStudio Host comes from its official release tarball (HOST_TGZ, checked against HOST_SHA256), unchanged.
# Host/LAYOUT names the layout (asb-state.sh: it wins over files a different earlier edition left behind).
#   host (default): Host/amistudio-host/ = that release unpacked as it is, including its engine (asb-chromed +
#                   ungoogled-chromium): one Chromium in the archive
#   own:            only amistudio-hostd from it; Host/asb-chromed (build/static) + Host/chromium (CHROMIUM_DIR), the
#                   layout before the Host shipped an engine. The Host/ scripts work with either (asb-state.sh picks).
set -eu
V=$1; UC=$2; LIC=$3; HOSTTGZ=$4; HOSTSHA=$5; MODE=${6:-host}; N=amisurf-amikit-host-$V-linux-x86_64
case $MODE in host|own) ;; *) echo "mode must be host or own"; exit 2 ;; esac
R=build/$N; D=$R/AmiSurf/Host
rm -rf $R && mkdir -p $D/LICENSES
echo "$HOSTSHA  $HOSTTGZ" | sha256sum -c - >/dev/null || { echo "$HOSTTGZ: SHA-256 is not $HOSTSHA"; exit 1; }
HT=$(mktemp -d); tar -C $HT -xzf "$HOSTTGZ"; HD=$(echo $HT/amistudio-host-*-linux-x86_64)
HV=$(basename "$HOSTTGZ" | sed 's/^amistudio-host-\(.*\)-linux-x86_64.tar.gz$/\1/')
ldd $HD/amistudio-hostd 2>&1 | grep -q 'not a dynamic' || { echo "amistudio-hostd in $HOSTTGZ is not static"; exit 1; }
if [ $MODE = host ]; then
  [ -f $HD/engine/engine.sh ] && [ -x $HD/engine/chromium/chrome ] || { echo "$HOSTTGZ has no engine/ (Host 0.1.2+ needed for mode host)"; exit 1; }
  mv $HD $D/amistudio-host   # the whole release folder, byte for byte
  HB=$D/amistudio-host/amistudio-hostd; WHERE="Host/amistudio-host/ is the AmiStudio Host $HV release folder, unchanged (engine/ = asb-chromed + Chromium, see its LICENSES/ENGINE-VERSION.txt)"
else
  cp -p $HD/amistudio-hostd $D/amistudio-hostd; [ -f $HD/LICENSES/AmiStudio-Host.txt ] && cp $HD/LICENSES/AmiStudio-Host.txt $D/LICENSES/
  cp $HD/README-HOST.md $D/LICENSES/AmiStudio-Host-README.md
  HB=$D/amistudio-hostd; WHERE="Host/amistudio-hostd is the AmiStudio Host $HV, unchanged from the official release"
  cp build/static/asb-chromed $D/; strip $D/asb-chromed
  mkdir -p $D/chromium; cp -a "$UC"/. $D/chromium/; rm -f $D/chromium/chromedriver
  cp "$LIC"/Chromium-LICENSE.txt "$LIC"/ungoogled-chromium-LICENSE.txt "$LIC"/portablelinux-LICENSE.txt "$LIC"/chromium-credits.html $D/LICENSES/
  cp $D/LICENSES/Chromium-LICENSE.txt $D/chromium/LICENSE
fi
{ echo "$WHERE:"
  echo "  https://github.com/hardcopi/AmiStudio-Releases/releases/tag/host-v$HV"
  echo "  $(basename "$HOSTTGZ")  sha256 $HOSTSHA"
  echo "  amistudio-hostd  sha256 $(sha256sum < $HB | cut -c1-64)"
  echo "  $($HB --help 2>&1 | head -1)"
  echo "Layout: $MODE (make AMIKIT_CHROMIUM=host|own)."
  if [ $MODE = host ]; then echo "Licences of the Host and the engine: Host/amistudio-host/LICENSES/."
  else echo "Host licence notes: AmiStudio-Host.txt; Host documentation: AmiStudio-Host-README.md."; fi
  echo "Source: https://github.com/hardcopi/AmiStudio-Host"
} > $D/LICENSES/AmiStudio-Host-VERSION.txt
rm -rf $HT
echo "$MODE" > $D/LAYOUT
cp dist/amikit/run-host.sh dist/amikit/engine.sh dist/amikit/move-to-host.sh dist/amikit/asb-state.sh dist/amikit/amikit-with-browser.sh dist/amikit/install-amikit.sh dist/amikit/get-widevine.sh dist/amikit/README-AMIKIT.txt $D/
chmod +x $D/run-host.sh $D/engine.sh $D/move-to-host.sh $D/amikit-with-browser.sh $D/install-amikit.sh $D/get-widevine.sh
cp dist/docs/ATTRIBUTIONS $D/LICENSES/AmiSurf-ATTRIBUTIONS.txt
bash tools/licenses.sh $D/LICENSES
if [ $MODE = own ]; then cp dist/licenses-host/*.txt $D/LICENSES/; fi   # stb_image, QOI, OpenSSL, zlib, zstd in asb-chromed
if find $R -iname '*widevine*' ! -name get-widevine.sh | grep -q .; then echo 'Widevine files in the bundle - refusing'; find $R -iname '*widevine*'; exit 1; fi
echo "chromium copies: $(find $R -name chrome -type f -path '*chromium*' | wc -l)"
tar -C $R -czf dist/$N.tar.gz --owner=0 --group=0 AmiSurf
ls -l dist/$N.tar.gz
