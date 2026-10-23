#!/bin/bash
# server side: fd.sh <func> [stockko] [builtko]: normalised instruction diff (- stock, + ours) of one function; branch targets and addresses stripped
f=$1; S=${2:-<work-dir> B=${3:-<work-dir>
O=<build-dir>
T=<work-dir>
for w in S B; do
  eval k=\$$w
  $O -d --no-show-raw-insn --disassemble-symbols=$f $k 2>/dev/null | grep -v -E '^$|Disassembly|file format|^[0-9a-f]+ <' | sed -E 's/^ *[0-9a-f]+:\s*//; s/\/\/.*$//; s/<[^>]*>//g; s/\s+$//; s/0x[0-9a-f]+$/L/; s/\s+/ /g' | sed -E 's/(R_AARCH64_\w+)\s+([^ +]+).*$/\1 \2/' > $T/fd_$w.txt
done
diff $T/fd_S.txt $T/fd_B.txt | head -${N:-40}
echo "stock $(wc -l < $T/fd_S.txt) ours $(wc -l < $T/fd_B.txt)"
