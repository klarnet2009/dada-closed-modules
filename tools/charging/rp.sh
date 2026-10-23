#!/bin/bash
cd <work-dir>
m=$1; shift
for f in "$@"; do echo "##### $m $f" >> ../rep/path_$m.txt; (ulimit -v 16000000; nice -n 10 timeout 1800 python3 -I pathcmp.py ../st/gl_$m.ko ../built/$m.ko $f 2>&1 | cut -c1-300 | head -60 >> ../rep/path_$m.txt); done
echo finished >> ../rep/path_$m.txt
