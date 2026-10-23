#!/usr/bin/env bash
# kb.sh <module-dir> [target] [stockname] [stubs-dir-to-upload]: g40c copy of the g14g flow; scratch <work-dir> pkg <dir>_g40c; uploads the stubs dir only when named (bq27z561: stubs_g14g, removed again by kbs.sh via STUBS_RM)
cd "/e/<workdir>/work/re/src" || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1; t=${2:-$1}; pkg=${m}_g40c
[ -n "${4:-}" ] && tar c "$4" | ssh $K $U "rm -rf <build-dir> mkdir -p <build-dir> && tar x -C <build-dir>"
scp -q $K ../g40c_tools/kbs.sh $U:<work-dir>
tar c --exclude=BUILD.bazel.cn06 --exclude=STATUS.md --exclude=attic --exclude='*.inc' --exclude='*.bak' --exclude='*.cn06' "$m" | ssh $K $U "rm -rf <build-dir> mkdir -p <build-dir> && tar x -C <build-dir> --strip-components=1"
ssh $K $U "SYMFN=\"${SYMFN:-}\" CW=${CW:-260} STUBS_RM=\"${4:-}\" bash <work-dir> $pkg $t ${3:-$t}"
