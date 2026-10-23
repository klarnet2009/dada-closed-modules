#!/usr/bin/env bash
while ! grep -q "^cleaned\|NO KO\|ABORT" /tmp/g22d_bk.log 2>/dev/null; do sleep 30; done
SYMFN="" bash kbw.sh mca_strategy_fg_comp > /tmp/g22d_fg.log 2>&1
