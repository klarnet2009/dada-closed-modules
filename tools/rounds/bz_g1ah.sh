#!/usr/bin/env bash
# bz.sh <module> [stubs] : upload work/re/src/<module> (+stubs_g1 if 2nd arg) and run Bazel build + compare_ko vs OS4 stock (tag g1)
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1
if [ -n "$2" ]; then tar c stubs_g1 | ssh $K $U 'tar x -C <build-dir>'; fi
tar c --exclude=STATUS.md "$m" | ssh $K $U "rm -rf <build-dir> tar x -C <build-dir>"
ssh $K $U "nice -n 10 <work-dir> $m" 2>&1
