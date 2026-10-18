#!/bin/bash
# lp.sh: pin __LINE__ + fast loop; prints errors or the per-function table of the differing functions
cd "/e/<workdir>/work/re/src"
python3 ../g13h_tools/pb_pin3.py ../g13h_tools/dump_mca_strategy_buckchg '[mca_strategy_buckchg]%s:%d ' mca_strategy_buckchg/mca_strategy_buckchg.c 2>&1 | head -1
INC=stubs_g13b bash ../g13h_tools/sm.sh mca_strategy_buckchg 2>&1 | tr ';' '\n' | grep -E "DIFF|total|error|MISSING" | tr '\n' ';'; echo
