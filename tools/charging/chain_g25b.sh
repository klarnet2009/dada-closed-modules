#!/bin/bash
cd "/e/<workdir>/work/re/g25b_tools" || exit 1
for m in "$@"; do echo "=== BUILD $m $(date)"; ./kbw.sh $m > out_$m.txt 2>&1; echo "=== done $m $(date)"; done
