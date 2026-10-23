#!/bin/bash
# half.sh <module> fn...: count signed-halving sequences (add reg,reg,reg lsr #31 ; asr #1) and '+1 then halve' in stock and ours
m=$1; shift
O=<build-dir>
for f in "$@"; do
 for w in S B; do
  if [ $w = S ]; then k=<work-dir> else k=<work-dir> fi
  $O -d --no-show-raw-insn --disassemble-symbols=$f $k 2>/dev/null | sed -E 's/^ *[0-9a-f]+:\s*//; s/\/\/.*$//; s/<[^>]*>//g' | awk '{p3=p2;p2=p1;p1=$0} /lsr[ \t]+w[0-9]+, w[0-9]+, #0x1f|add.*lsr #31|add.*, lsr #0x1f/{h++} /asr[ \t]+w[0-9]+, w[0-9]+, #0x1$|asr[ \t]+w[0-9]+, w[0-9]+, #1$/{a++; if (p2 ~ /add[ \t]+w[0-9]+, w[0-9]+, #0x1$/ || p3 ~ /add[ \t]+w[0-9]+, w[0-9]+, #0x1$/) q++} END{printf "%s lsr31=%d asr1=%d plus1_before_asr=%d\n", "'$w'", h, a, q}'
 done | tr "\n" " "; echo " $f"
done
