#!/bin/bash
# cmpf.sh <module> <func> : lst2 listings (stock vs our last fast-loop object) + ldiff
S="<scratch>"
D="$(dirname $0)"
bash "$D/sv.sh" "cd <work-dir> && python3 bin/lst2.py stock/$1.ko $2" > "$S/$2.s.txt"
bash "$D/sv.sh" "cd <work-dir> && python3 bin/lst2.py ${OBJ:-o/$1.lto.o} $2" > "$S/$2.o.txt"
python3 "$D/ldiff.py" "$S/$2.s.txt" "$S/$2.o.txt"
