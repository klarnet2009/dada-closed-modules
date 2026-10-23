#!/usr/bin/env bash
T=<work-dir> cd $T
for m in "$@"; do
  fl=$(cd tools2 && python3 -I compare_ko_v2.py ../stock/$m.ko ../built/$m.ko -v 2>&1 | grep "FAIL function-bytes" | sed -E 's/^.*differ: //' | tr -d ',')
  echo "$m: $fl" > res/$m.fl
  : > res/$m.sym
  for f in $fl; do
    (ulimit -v 9000000; timeout 200 python3 -I tools2/symcmp4.py stock/$m.ko built/$m.ko $f 2>&1 | cut -c1-400 >> res/$m.sym) || echo "=== $f: TIMEOUT/ERR" >> res/$m.sym
  done
  echo "== $m"; grep -E "^=== |SUMMARY [a-z_A-Z0-9]+:|TIMEOUT" res/$m.sym | sed -E 's/SUMMARY ([a-zA-Z_0-9]+): effects/S \1/' | cut -c1-220
done
echo SYMDONE
