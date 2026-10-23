#!/bin/bash
# mkhack.sh : classify the no-hack peach_v2 build, make the line-hack json, create wlan/hack (copy of pkg + hack) and the symlink qcawlan_g40dh
A=<work-dir> B=<build-dir>
cd $A; ulimit -v 16000000; cp dump/b_peach_v2.diff dump/b_pv2.diff
nice -n 10 python3 scripts/offpairs2.py peach_v2 built/b_peach_v2.ko dump/ours_b.dis dump/b_pv2.diff > logs/b_cls.txt 2>&1
python3 scripts/linehack.py gen dump/b_pv2.diff.cls dump/lh_b.json wlan/pkg/qcacld3 > logs/b_lh.txt 2>&1
rm -rf wlan/hack; cp -r wlan/pkg wlan/hack
python3 scripts/linehack.py apply dump/lh_b.json wlan/hack/qcacld3 >> logs/b_lh.txt 2>&1
sed -i "s/qcawlan_g40d/qcawlan_g40dh/g" wlan/hack/qcacld3/g4_modules.bzl
[ -e <build-dir> ] || ln -s $A/wlan/hack <build-dir>
echo MKHACK_DONE >> logs/b_lh.txt
