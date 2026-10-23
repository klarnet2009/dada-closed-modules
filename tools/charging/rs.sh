#!/bin/bash
# rs.sh <module> fn... : symcmp5b (atoms mode) per function; env TMO (default 900)
cd <work-dir>
m=$1; shift
: > ../rep/${m}_s5.txt
for f in "$@"; do echo "##### $f" >> ../rep/${m}_s5.txt; (ulimit -v 16000000; S5ATOMS=1 nice -n 10 timeout ${TMO:-900} python3 -I symcmp5b.py ../st/gl_$m.ko ../built/$m.ko $f 2>&1 | cut -c1-400 >> ../rep/${m}_s5.txt); done
echo finished >> ../rep/${m}_s5.txt
