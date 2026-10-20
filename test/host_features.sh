#!/bin/bash
# host_features.sh — integration test for the 0.68 host features (needs Chrome, or Firefox with ASB_TEST_BROWSER=firefox, + an X display).
# Starts its own asb-chromed on a spare port with a throwaway config/profile, drives it with tools/asbc_probe.py,
# and stops only the PID it started.   usage: make test-host   (ASB_CHROME_DISPLAY=:N to pick the display)
set -u
cd "$(dirname "$0")/.."
P=${ASB_TEST_PORT:-9253}; T=$(mktemp -d); fails=0
ASB_CONFIG=$T/cfg build/host/asb-chromed -browser ${ASB_TEST_BROWSER:-chrome} -port $P -cdp $((P-1)) -profile $T/prof >$T/host.log 2>&1 & HP=$!
trap 'kill $HP 2>/dev/null; wait $HP 2>/dev/null; sleep 1; rm -rf "$T" 2>/dev/null' EXIT
for i in $(seq 40); do (exec 3<>/dev/tcp/127.0.0.1/$P) 2>/dev/null && break; sleep .5; done
cat >$T/p.html <<'H'
<html><head><title>T</title></head><body><a href="http://example.com/" style="display:block;width:300px;height:100px">link</a> foo foo foo<img src="data:image/gif;base64,R0lGODlhAQABAAAAACw=" width=50 height=50></body></html>
H
U="file://$T/p.html"
out=$(python3 tools/asbc_probe.py $P "nav $U" 'wait 4' 'cmd zoom 125' 'wait 1' 'find foo' 'wait 2' 'cmd findnext' 'wait 1' 'cmd ctx 40 40' 'wait 1' \
  'cmd bookmark' 'wait 1' 'cmd suggest file' 'wait 1' 'nav asb://bookmarks' 'wait 3' 'cmd zoom 0' 'wait 1' 2>&1)
chk(){ if grep -qE "$1" <<<"$out"; then echo "ok   $2"; else echo "FAIL $2"; fails=$((fails+1)); fi; }
chk 'ZOOM 125' 'zoom 125%'
chk 'FINDRES [0-9]+/3' 'find count (3 matches)'
chk 'FINDRES 2/3' 'find next'
chk 'CTX .*example.com' 'context menu link'
chk 'SUGG .*p.html' 'address suggestions from bookmarks/history'
chk 'META .*asb://bookmarks' 'internal page shown as asb://'
chk 'ZOOM 100' 'zoom reset'
grep -q "p.html" $T/cfg/bookmarks.tsv 2>/dev/null && echo "ok   bookmark saved" || { echo "FAIL bookmark saved"; fails=$((fails+1)); }
code=$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: evil.example' http://127.0.0.1:$((P+1))/bookmarks)
[ "$code" = 403 ] && echo "ok   pages server rejects foreign Host (DNS rebinding)" || { echo "FAIL Host check ($code)"; fails=$((fails+1)); }
curl -s -o /dev/null "http://127.0.0.1:$((P+1))/bookmarks?del=0&k=wrong"
grep -q "p.html" $T/cfg/bookmarks.tsv && echo "ok   delete with a wrong key refused (CSRF)" || { echo "FAIL CSRF: bookmark deleted without the key"; fails=$((fails+1)); }
[ -n "${ASB_TEST_VERBOSE:-}" ] && echo "$out"
echo "$([ $fails = 0 ] && echo 'all passed' || echo FAILED) ($fails failed)"; exit $((fails>0))
