#!/usr/bin/env bash
# kb.sh <module-dir> [target] [stockname]: upload work/re/src/<dir> as //re/<dir>_g13d, real Kleaf build, compare_ko_v2 + preamble_cmp, delete bazel-out
cd "/e/<workdir>/work/re/src" || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1; t=${2:-$1}; pkg=${m}_g13d
scp -q $K ../g13d_tools/kbs.sh $U:<work-dir>
tar c --exclude=STATUS.md --exclude=attic --exclude='*.inc' --exclude='*.bak' "$m" | ssh $K $U "rm -rf <build-dir> mkdir -p <build-dir> && tar x -C <build-dir> --strip-components=1"
ssh $K $U "CW=${CW:-300} N=${N:-45} bash <work-dir> $pkg $t ${3:-$t}"
