#!/bin/bash
# a.sh <module> <func> [from] [to]: annotated stock listing (dev struct members)
m=${MOD:-mca_strategy_buckchg}; S=${STRUCT:-strategy_buckchg_dev}
python3 "$(dirname $0)/an.py" "$(dirname $0)/dump_$m" $S "$@"
