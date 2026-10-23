#!/bin/bash
# rbm.sh <module>: (server) real Kleaf build via g2l rb.sh + compare_ko_v2 + preamble_cmp, then delete the bazel output dir
m=$1; A=<work-dir>
<work-dir> $m 2>&1 | cut -c1-400
echo "=== compare_ko_v2"
python3 -I $A/bin/compare_ko_v2.py $A/stock/$m.ko $A/built/$m.ko 2>&1 | tail -${V2TAIL:-40}
echo "=== preamble_cmp"
python3 <work-dir> $A/stock/$m.ko $A/built/$m.ko 2>&1 | tail -3
P=<build-dir> chmod -R u+w $P 2>/dev/null; rm -rf $P
echo ALLDONE
