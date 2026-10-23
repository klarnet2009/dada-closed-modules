#!/usr/bin/env bash
# fnc.sh <module> <func> [maxlines] [stock|cn]: compact stock disassembly: relocs merged onto the instruction line (adrp-only first reloc kept), branch targets as offsets
D="$(dirname "$0")"
bash $D/fn.sh "$1" "$2" 4000 | python3 -c '
import sys,re
out=[]
for l in sys.stdin:
    l=l.rstrip("\n")
    m=re.match(r"^\s*;; (\w+)\s+(.*)$",l)
    if m:
        if m.group(1) in ("ADD_ABS_LO12_NC",) or m.group(1).startswith("LDST") and out and "adrp" not in out[-1]:
            if m.group(1)=="ADD_ABS_LO12_NC": continue
        if out: out[-1]+="   ;; "+m.group(2)
        continue
    out.append(l)
print("\n".join(out))
' | head -${3:-600}
