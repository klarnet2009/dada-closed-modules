#!/bin/bash
# fd.sh <module> <func>: normalised instruction diff (- stock, + ours)
m=$1; f=$2
O=<build-dir>
T=<work-dir>
for w in S B; do
  if [ $w = S ]; then k=<work-dir> else k=<work-dir> fi
  $O -d --no-show-raw-insn --disassemble-symbols=$f $k 2>/dev/null | grep -v -E '^$|Disassembly|file format|^[0-9a-f]+ <' | sed -E 's/^ *([0-9a-f]+):\s*/\1 /; s/\/\/.*$//; s/<[^>]*>//g; s/\s+$//; s/\s+/ /g' > $T/L_$w.txt
done
