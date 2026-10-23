#!/bin/bash
# tbls.sh <prefix> chips... : data-table audits of each finished build (waits for the chain log)
A=<work-dir> P=$1; shift
cd $A; ulimit -v 16000000
for c in "$@"; do
  until grep -q "$P $c end" logs/chain_$P.txt 2>/dev/null; do sleep 20; done
  nice -n 10 python3 -I scripts/tbl_audit3.py stock/qca_cld3_$c.ko built/${P}_$c.ko > tbl/a3_${P}_$c.txt 2>&1
  nice -n 10 python3 -I scripts/tbl_audit.py stock/qca_cld3_$c.ko built/${P}_$c.ko > tbl/a1_${P}_$c.txt 2>&1
  nice -n 10 timeout 900 python3 -I scripts/anon_pair2.py stock/qca_cld3_$c.ko built/${P}_$c.ko > tbl/a2_${P}_$c.txt 2>&1
  nice -n 10 python3 -I scripts/compare_ko_v2.py stock/qca_cld3_$c.ko built/${P}_$c.ko > tbl/cmp_${P}_$c.txt 2>&1
  echo "$c done" >> logs/tbls_$P.txt
done
