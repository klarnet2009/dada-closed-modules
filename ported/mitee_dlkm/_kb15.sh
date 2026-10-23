#!/usr/bin/env bash
# _kb15.sh: real Kleaf build of work/re/src/mitee_dlkm + stubs (tag g12), compare_ko_v2 + preamble_cmp vs OS4 stock, delete bazel-out
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
tar c --exclude=STATUS.md --exclude=_g15.sh --exclude=_kb15.sh --exclude=attic mitee_dlkm | ssh $K $U "rm -rf <build-dir> mkdir -p <build-dir> && tar x -C <build-dir> --strip-components=1"
ssh $K $U "CW=${CW:-300} N=${N:-40} bash <work-dir> mitee_dlkm_g15b mitee_dlkm mitee_dlkm"
