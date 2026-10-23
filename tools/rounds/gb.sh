#!/usr/bin/env bash
# gb.sh <module> [vendor_ramdisk|vendor_dlkm]  (runs ON THE SERVER, from <work-dir>)
# Kleaf DDK build of //re/<module>:<module>, then compare_ko_v2 + preamble_cmp against the GLOBAL OS4.0.0.7 stock, then deletes bazel-out of the package.
set -uo pipefail
m=$1; part=${2:-vendor_ramdisk}; W=<build-dir>; T=<work-dir> S=<work-dir>
[ "$(df --output=avail -BG / | tail -1 | tr -dc 0-9)" -ge 4 ] || { echo "ABORT: server disk <4G free"; exit 3; }
cd $W
BZ="tools/bazel --output_user_root=$W/.bazel_root"
$BZ build --lto=none --config=stamp //re/$m:$m 2>&1 | grep -vE "warning: (ignoring|overriding)|^--|STABLE_SCMVERSIONS|^\[|^INFO: (Invocation|Analyzed|Found|From|Build option|Streaming)|^Loading|^Analyzing|^DEBUG" | grep -E "error|ERROR|Error|undefined|Build completed|fatal" | cut -c1-400 | head -${LINES_MAX:-30}
P=$($BZ cquery --lto=none --config=stamp --output=files //re/$m:$m 2>/dev/null | head -1)
[ -n "$P" ] && [ -f "$P" ] || { echo "NO .ko PRODUCED"; exit 1; }
mkdir -p $T/built; cp "$P" $T/built/$m.ko
D=$W/bazel-out/k8-fastbuild/bin/re/$m; [ -d "$D" ] && { chmod -R u+w "$D"; rm -rf "$D"; }
cd $T/tools
python3 -I $S/tools/compare_ko_v2.py $S/mods/$part/$m.ko $T/built/$m.ko ${CMP_V:--v} > $T/cmp_$m.txt 2>&1
grep -E "^(STRICT|VERDICT|NOT|Verdict)|FAIL|PASS" $T/cmp_$m.txt | cut -c1-300 | head -40
python3 -I $S/tools/preamble_cmp.py $S/mods/$part/$m.ko $T/built/$m.ko
