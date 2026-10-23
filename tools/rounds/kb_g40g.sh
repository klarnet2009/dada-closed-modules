#!/usr/bin/env bash
# kb.sh <module-dir> [target] [stockname]: upload work/re/src/<dir> as //re/<dir>_g40g:<target>, real Kleaf build, compare_ko_v2 + preamble_cmp, delete bazel-out
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1; t=${2:-$1}; pkg=${m}_g40g
scp -q $K g40g_tools/kbs.sh $U:<work-dir>
tar c --exclude=STATUS.md --exclude=attic "$m" | ssh $K $U "rm -rf <build-dir> mkdir -p <build-dir> && tar x -C <build-dir> --strip-components=1 && sed -i s#//re/stubs_a17C:stub_machine#//re/stubs_g40g:stub_machine# <build-dir>"
tar c stubs_g40g | ssh $K $U "rm -rf <build-dir> tar x -C <build-dir>"
ssh $K $U "CW=${CW:-300} N=${N:-40} VERB=${VERB:-} bash <work-dir> $pkg $t ${3:-$t}"
