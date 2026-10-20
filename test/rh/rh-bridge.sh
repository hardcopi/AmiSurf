#!/bin/bash
# rh-bridge.sh HDDIR : Linux side of the Rabbit Hole stand-in. Runs each SYS:T/hostrun.req, mapping System:/SYS: to HDDIR
D=$1; while :; do
  if [ -f "$D/T/hostrun.req" ]; then
    mapfile -d $'\x1f' -t A < "$D/T/hostrun.req"; rm -f "$D/T/hostrun.req"
    A[-1]=${A[-1]%$'\n'}
    for i in "${!A[@]}"; do case "${A[$i]}" in System:*|SYS:*) A[$i]="$D/${A[$i]#*:}";; esac; done
    echo "$(date +%T) host-run: ${A[*]}"; "${A[@]}" &
  fi; sleep 0.2; done
