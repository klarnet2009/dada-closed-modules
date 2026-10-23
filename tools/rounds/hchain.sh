#!/bin/bash
A=<work-dir> cd $A
until grep -q CHAIN_DONE logs/chain_b.txt 2>/dev/null; do sleep 20; done
until grep -q MKHACK_DONE logs/b_lh.txt 2>/dev/null; do sleep 20; done
scripts/bchain.sh h h peach_v2 peach kiwi_v2 qca6750 wcn7750
