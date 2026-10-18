#!/usr/bin/env bash
# server side of kb.sh: kbs.sh <pkg> <target> [stockname]
set -uo pipefail
pkg=$1; t=$2; s=${3:-$2}; W=<build-dir>; T=<work-dir>
[ "$(df --output=avail -BG / | tail -1 | tr -dc 0-9)" -ge 4 ] || { echo ABORT disk; exit 3; }
cd $W
BZ="tools/bazel --output_user_root=$W/.bazel_root"
nice -n 10 $BZ build --lto=none --config=stamp //re/$pkg:$t 2>&1 | grep -E 'error|ERROR|undefined|Build completed|fatal' | cut -c1-300 | head -20
P=$($BZ cquery --lto=none --config=stamp --output=files //re/$pkg:$t 2>/dev/null | head -1)
[ -n "$P" ] && [ -f "$P" ] || { echo NO KO; exit 1; }
mkdir -p $T/built; cp "$P" $T/built/$t.ko; md5sum $T/built/$t.ko
cd $T/tools2 && python3 -I compare_ko_v2.py ../stock/$s.ko ../built/$t.ko ${VERB:-} 2>&1 | cut -c1-${CW:-300} | head -${N:-40}; python3 -I preamble_cmp.py ../stock/$s.ko ../built/$t.ko 2>&1 | cut -c1-600
P2=$W/bazel-out/k8-fastbuild/bin/re/$pkg; [ -d $P2 ] && { chmod -R u+w $P2; rm -rf $P2; echo cleaned; }; rm -rf $W/re/$pkg; df -h / | tail -1
