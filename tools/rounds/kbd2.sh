#!/usr/bin/env bash
# kbd2.sh: G22b D2 build of mi_reclaim against a stub provider named xr_qi (depends=xr_qi), real Kleaf build + compare_ko_v2 + preamble_cmp, cleans up.
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
scp -q $K g22b_tools/kbs.sh $U:<work-dir>
tar c --exclude=STATUS.md mi_reclaim | ssh $K $U "rm -rf <build-dir> mkdir -p <build-dir> && tar x -C <build-dir> --strip-components=1 && sed -i 's#//re/stubs_g1/os4:os4_xr_qi#//re/xrqi_g22b:xr_qi_stub#' <build-dir>"
tar c xrqi_stub 2>/dev/null | true
ssh $K $U "rm -rf <build-dir> mkdir -p <build-dir> cp <build-dir> <build-dir>"
scp -q $K g22b_tools/xrqi_stub/BUILD.bazel $U:<build-dir>
# kbs.sh deletes only the module package; the stub package is removed below
ssh $K $U "mkdir -p <work-dir> [ -f <work-dir> ] || cp <work-dir> <work-dir> CW=${CW:-300} N=${N:-40} bash <work-dir> mi_reclaim_g22b mi_reclaim mi_reclaim"
ssh $K $U "P=<build-dir> [ -d \$P ] && { chmod -R u+w \$P; rm -rf \$P; }; rm -rf <build-dir> strings -a <work-dir> | grep -E '^(depends|name)='"
