#!/bin/bash
# ndc.sh <func>: normalized diff line count only
cd "/e/<workdir>/work/re/src"
python3 ../g13p_tools/pb_pin3.py ../g13p_tools/dump_mca_strategy_buckchg '[mca_strategy_buckchg]%s:%d ' mca_strategy_buckchg/mca_strategy_buckchg.c >/dev/null 2>&1
CTX=60 INC=stubs_g13b bash ../g13p_tools/fgq.sh mca_strategy_buckchg "$@" 2>&1 | sed -n '/^=== /,$p' | python3 ../g13p_tools/ndiff.py 3 ${LIM:-0} | grep "^==="
