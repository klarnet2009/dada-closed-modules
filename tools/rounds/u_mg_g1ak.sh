#!/usr/bin/env bash
# u_mg.sh : push mi_game, build the unity TU (stock-local functions static) with the ThinLTO flags, run fdiff vs stock. env SKIP/RENVAR as in unity_mg.py
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=mi_game
tar c --exclude=STATUS.md "$m" | ssh $K $U "rm -rf <work-dir> tar x -C <work-dir>"
scp -q $K g1ak_tools/unity_mg.py $U:<work-dir>
ssh $K $U "cd <work-dir> && find src/$m -type f | xargs sed -i 's/\r\$//'; SKIP='$SKIP' RENVAR='$RENVAR' python3 tools2/unity_mg.py src/$m src/${m}_u stock/$m.ko && cd tools && MODNAME=$m ./pb_cc.sh <work-dir> <work-dir> -flto=thin -Wno-unused-variable -Wno-unused-function -Wno-unused-const-variable -Wno-cast-function-type -Wno-cast-function-type-strict 2>&1 | grep -E 'error' | head -\${N:-20}; cd .. && <build-dir> -r -mllvm -import-instr-limit=5 -o o/${m}_u.o o/${m}_u.lto.o && python3 tools/fdiff.py stock/$m.ko o/${m}_u.o | grep -vE '^(RELOC|add\s+x[0-9]+, x[0-9]+, #0x0)' | tail -\${HEADN:-12} ; python3 -I tools2/compare_ko_v2.py stock/$m.ko o/${m}_u.o | grep -E '^(PASS|FAIL)' | cut -c1-\${CW:-240}"
