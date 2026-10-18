#!/bin/bash
# ident.sh <module>: summary of the ThinLTO-emulation compare: identical count / written-but-different with word diffs / missing count
cd "$(dirname "$0")/.." || exit 1
m=$1
out=$(TAILN=2000 HEADN=2000 bash g11b_tools/qq.sh $m 2>&1)
echo "$out" | grep -E "error" | head -5
miss=$(echo "$out" | grep -c "MISSING")
tot=$(ssh -i <ssh-key-file> -o BatchMode=yes <user>@<build-server> "python3 - <<'P'
import sys
sys.path.insert(0,'<work-dir>')
from compare_ko import Elf
print(len(Elf('<work-dir>').functions()))
P")
echo "stock functions: $tot, MISSING: $miss"
echo "$out" | grep -E "worddiff" | awk '{split($2,a,"="); printf "%s(%s) ", $1, a[2]}'
echo
echo "printed(worddiff or reloc): $(echo "$out" | grep -c worddiff)  -> identical = $((tot - miss - $(echo "$out" | grep worddiff | grep -vc "worddiff=0 ")))"
