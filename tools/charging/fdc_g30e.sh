#!/bin/bash
# fdc.sh <module> <func> [maxlines]: diff of condition-related instructions only (cmp/b.cc/cb/tb/csel/cset/ccmp/mov imm/bl targets), normalised
m=$1; f=$2
O=<build-dir>
T=<work-dir>
for w in S B; do
  if [ $w = S ]; then k=<work-dir> else k=<work-dir> fi
  $O -d -r --no-show-raw-insn --disassemble-symbols=$f $k 2>/dev/null | grep -v -E '^$|Disassembly|file format|^[0-9a-f]+ <' | awk 'BEGIN{seen=0} /R_AARCH64/{ if(seen && /CALL26|JUMP26/) print "BL " $NF; next } {seen=1; print}' | sed -E 's/^ *[0-9a-f]+:\s*//; s/\/\/.*$//; s/<[^>]*>//g; s/\s+$//; s/0x[0-9a-f]+$/L/; s/\s+/ /g' | grep -E '^(cmp|cmn|tst|ccmp|ccmn|b\.|cbz|cbnz|tbz|tbnz|csel|csinc|csinv|cset|cinc|BL|bl|subs|adds)' | sed -E 's/\b[wx]([0-9]+)\b/r/g' > $T/C_$w.txt
done
diff $T/C_S.txt $T/C_B.txt > $T/C_d.txt
echo "stock $(wc -l < $T/C_S.txt) ours $(wc -l < $T/C_B.txt) difflines $(grep -c '^[<>]' $T/C_d.txt)"
grep -E '^[<>]' $T/C_d.txt | head -${3:-60}
