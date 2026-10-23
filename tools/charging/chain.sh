#!/bin/bash
cd "/e/<workdir>/work/re/g22c_tools" || exit 1
wl() { while true; do L=$(/tmp/ss22.sh "cut -d' ' -f1 /proc/loadavg" | cut -d. -f1); [ "${L:-99}" -lt 20 ] && break; sleep 30; done; }
for m in "$@"; do wl; echo "=== BUILD $m $(date)"; ./kb.sh $m > out_$m.txt 2>&1; echo "=== done $m $(date)"; done
