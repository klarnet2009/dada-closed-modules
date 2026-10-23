#!/usr/bin/env bash
# v2.sh <module> [stockname]: compare_ko_v2 (strict) + preamble_cmp of <work-dir> vs the OS4 stock
D="$(dirname "$0")"; m=$1; s=${2:-$1}
bash $D/sv.sh "cd <work-dir> && python3 -I compare_ko_v2.py ../stock/$s.ko ../built/$m.ko 2>&1 | cut -c1-${W:-400} | head -${N:-40}; python3 -I preamble_cmp.py ../stock/$s.ko ../built/$m.ko 2>&1 | cut -c1-600"
