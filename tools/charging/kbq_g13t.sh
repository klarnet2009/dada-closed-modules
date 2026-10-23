#!/usr/bin/env bash
# kb.sh <module-dir> [target] [stockname]: upload work/re/src/<dir> as //re/<dir>_g13t, real Kleaf build, compare_ko_v2 -v + preamble + symcmp of every differing function, delete bazel-out
cd "/e/<workdir>/work/re/src" || exit 1
python3 ../g13l_tools/pb_pin3.py ../g13s_tools/dump_bq27z561 '[fuelgauge_ic]%s:%d ' bq27z561/bq27z561.c 2>&1 | head -3
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1; t=${2:-$1}; pkg=${m}_g13t
scp -q $K ../g13t_tools/kbs.sh $U:<work-dir>
tar c --exclude=STATUS.md --exclude=attic --exclude='*.inc' --exclude='*.bak' "$m" | ssh $K $U "rm -rf <build-dir> mkdir -p <build-dir> && tar x -C <build-dir> --strip-components=1"
ssh $K $U "SYMFN=\"${SYMFN:-}\" CW=${CW:-260} bash <work-dir> $pkg $t ${3:-$t}"
