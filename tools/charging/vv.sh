#!/bin/bash
# vv.sh q|b|s <func> [from] [to]: annotated + call-summarized stock listing (no frame boilerplate)
case $1 in q) export MOD=mca_quick_wireless STRUCT=mca_wireless_quick_charge_info;; b) export MOD=mca_basic_wireless STRUCT=strategy_wireless_dev;; s) export MOD=mca_smart_charge STRUCT=smart_charge_info;; esac
shift; export REGBASE=${REGBASE:-x19:0,x0:0}; D="$(dirname $0)"
bash $D/a.sh "$@" | python3 $D/sum.py | grep -v -E "^[0-9a-f]+: (stp|ldp|sub sp|add sp|mov x29|add x29|ldr x[0-9]+, \[sp|str x[0-9]+, \[sp|mrs|\.word|paciasp|autiasp|stur x[0-9]+, \[x29|ldur x[0-9]+, \[x29)" | cut -c1-${W:-190}
