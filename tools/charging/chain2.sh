#!/usr/bin/env bash
# waits for the revchg build (chain 1) to finish, then smart_charge and buckchg builds, one at a time
while ! grep -q "^cleaned\|NO KO\|ABORT" /tmp/g22d_revchg.log 2>/dev/null; do sleep 30; done
SYMFN="smart_charge_check_bypass_status" bash kbw.sh mca_smart_charge > /tmp/g22d_sc.log 2>&1
SYMFN="strategy_buckchg_set_charge_volt strategy_buckchg_process_antiburn_change" bash kbw.sh mca_strategy_buckchg > /tmp/g22d_bk.log 2>&1
