#!/bin/bash
# symat.sh <module> <hexaddr...>: symbol containing the .text address
m=${MOD:-mca_strategy_buckchg}; MOD_ARGS="$@"
awk '$4=="FUNC"{print $2,$3,$8}' "$(dirname $0)/dump_$m/syms.txt" | python3 -c "
import sys
t=[(int(a,16),int(s),n) for a,s,n in (l.split() for l in sys.stdin)]
for q in sys.argv[1:]:
    v=int(q,16)
    for a,s,n in t:
        if a<=v<a+s: print(q,n,'+0x%x'%(v-a))
" "$@"
