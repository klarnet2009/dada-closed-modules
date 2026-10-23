#!/bin/bash
# fdn.sh <module> <func> [maxlines]: normalised diff (addresses/targets stripped, relocs kept as symbol)
m=$1; f=$2
O=<build-dir>
T=<work-dir>
for w in S B; do
  if [ $w = S ]; then k=<work-dir> else k=<work-dir> fi
  $O -d --no-show-raw-insn --disassemble-symbols=$f $k 2>/dev/null | grep -v -E '^$|Disassembly|file format|^[0-9a-f]+ <' | sed -E 's/^ *[0-9a-f]+:\s*//; s/\/\/.*$//; s/<[^>]*>//g; s/\s+$//; s/0x[0-9a-f]+$/L/; s/\s+/ /g' > $T/N_$w.txt
done
diff $T/N_S.txt $T/N_B.txt | head -${3:-80}
echo "stock $(wc -l < $T/N_S.txt) ours $(wc -l < $T/N_B.txt)"
