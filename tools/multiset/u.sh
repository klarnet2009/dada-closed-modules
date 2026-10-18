#!/usr/bin/env bash
# u.sh : push matt, build the unity-TU (stock-local functions static) with the ThinLTO flags, run fdiff vs stock. env SKIP="names" to keep some non-static. prints errors first
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=matt
for f in $m/*.c $m/*.h; do tr -d '\r' < $f > $f.tmp && mv $f.tmp $f; done
tar c --exclude=STATUS.md "$m" | ssh $K $U "rm -rf <work-dir> tar x -C <work-dir>"
scp -q $K g1ad_tools/unity.py $U:<work-dir>
ssh $K $U "cd <work-dir> && NOTU="$NOTU" SKIP="$SKIP" python3 tools2/unity.py src/$m src/${m}_u stock/$m.ko && cd tools && MODNAME=$m ./pb_cc.sh <work-dir> <work-dir> -flto=thin -Wno-unused-variable -Wno-unused-function -Wno-unused-const-variable -Wno-cast-function-type -Wno-cast-function-type-strict 2>&1 | grep -E 'error' | head -${N:-20}; cd .. && <build-dir> -r -mllvm -import-instr-limit=5 -o o/${m}_u.o o/${m}_u.lto.o && python3 tools/fdiff.py stock/$m.ko o/${m}_u.o | head -${HEADN:-3000} && python3 -I tools2/compare_ko_v2.py stock/$m.ko o/${m}_u.o | grep -E '^(PASS|FAIL)' | cut -c1-200"
