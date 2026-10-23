#!/bin/bash
cd <work-dir>
ulimit -v 16000000
while read m st o; do
  [ -f out/$m.txt ] && continue
  nice -n 10 timeout 900 python3 -I tbl_audit3.py <work-dir> <work-dir> > out/$m.txt 2>&1
  nice -n 10 timeout 600 true
done < pairs.txt
echo done > out/ALLDONE
