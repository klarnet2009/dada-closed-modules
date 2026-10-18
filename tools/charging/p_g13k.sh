#!/bin/bash
# p.sh q|b|s [maxdiff]: pin __LINE__ + fast loop (errors, or MISSING + per-function diff table)
cd "/e/<workdir>/work/re/src"
case $1 in q) m=mca_quick_wireless;; b) m=mca_basic_wireless;; s) m=mca_smart_charge;; esac
python3 ../g13k_tools/pb_pin3.py ../g13k_tools/dump_$m "[$m]%s:%d " $m/$m.c 2>&1 | head -2
INC=stubs_g13a bash ../g13k_tools/sm.sh $m $2 2>&1
