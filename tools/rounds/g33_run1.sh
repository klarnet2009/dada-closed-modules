#!/bin/bash
cd <work-dir> ulimit -v 16000000
m=$1
st=$(grep "^$m " pairs.txt | cut -d" " -f2); o=$(grep "^$m " pairs.txt | cut -d" " -f3)
python3 -I tbl_audit3.py <work-dir> <work-dir> 2>&1 | cut -c1-${2:-360} | sed -n '/== 2/,$p'
