#!/bin/bash
# bchain.sh <tag-suffix: "" or h> <prefix> chips... : sequential real Kleaf builds (nice, wait load<20), compare with the stock, bazel-out removed
A=<work-dir> W=<build-dir>
TAGX=$1; PFX=$2; shift 2
for c in "$@"; do
  while true; do l=$(cut -d" " -f1 /proc/loadavg); [ ${l%.*} -lt 20 ] && break; sleep 20; done
  echo "=== $PFX $c start $(date +%H:%M:%S) load $l" >> $A/logs/chain_$PFX.txt
  TAG=g40d$TAGX $A/scripts/wbuild.sh $c > $A/logs/b_${PFX}_$c.txt 2>&1
  cp $A/built/qca_cld3_$c.ko $A/built/${PFX}_$c.ko
  python3 $A/scripts/fcmp.py $A/stock/qca_cld3_$c.ko $A/built/${PFX}_$c.ko $A/dump/${PFX}_$c >> $A/logs/chain_$PFX.txt 2>&1
  python3 $A/scripts/preamble_cmp.py $A/stock/qca_cld3_$c.ko $A/built/${PFX}_$c.ko >> $A/logs/chain_$PFX.txt 2>&1
  tail -3 $A/logs/b_${PFX}_$c.txt >> $A/logs/chain_$PFX.txt
  echo "=== $PFX $c end $(date +%H:%M:%S)" >> $A/logs/chain_$PFX.txt
done
echo CHAIN_DONE >> $A/logs/chain_$PFX.txt
