#!/bin/bash
# q.sh q|b|s <func> [from] [to]: annotated stock listing, REGBASE x19/x0 = 0, no frame boilerplate
case $1 in q) export MOD=mca_quick_wireless STRUCT=mca_wireless_quick_charge_info;; b) export MOD=mca_basic_wireless STRUCT=strategy_wireless_dev;; s) export MOD=mca_smart_charge STRUCT=smart_charge_info;; esac
shift
export REGBASE=${REGBASE:-x19:0,x0:0}
bash "$(dirname $0)/r.sh" "$@"
