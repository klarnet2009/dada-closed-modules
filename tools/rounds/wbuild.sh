#!/usr/bin/env bash
# wbuild.sh <chip e.g. peach_v2>: ONE Kleaf build of //re/qcawlan_${TAG:-g40d}/qcacld3:qca_cld3_<chip> (nice), copy the .ko to <work-dir>
# fcmp function-set report; the bazel-out of the package is deleted afterwards unless KEEP=1.
set -uo pipefail
c=$1; L=//re/qcawlan_${TAG:-g40d}/qcacld3:qca_cld3_$c; W=<build-dir>; A=<work-dir>
[ "$(df --output=avail -BG / | tail -1 | tr -dc 0-9)" -ge 6 ] || { echo "ABORT: server disk <6G free"; exit 3; }
cd $W
BZ="tools/bazel --output_user_root=$W/.bazel_root"
nice -n 10 $BZ build --lto=none --config=stamp $L 2>&1 | grep -vE "warning: (ignoring|overriding)|^--|STABLE_SCMVERSIONS|^\[|^INFO: (Invocation|Analyzed|Found|From|Build option|Streaming)|^Loading|^Analyzing|^DEBUG" | grep -E "error|ERROR|Error|undefined|Build completed|fatal|no such|missing" | cut -c1-600 | head -${NL:-40}
P=$($BZ cquery --lto=none --config=stamp --output=files $L 2>/dev/null | head -1)
[ -n "$P" ] && [ -f "$P" ] || { echo "NO .ko PRODUCED"; exit 1; }
cp "$P" $A/built/qca_cld3_$c.ko
if [ -n "${KEEP:-}" ]; then echo "kept bazel-out: $(dirname $P)"; fi
python3 $A/scripts/fcmp.py $A/stock/qca_cld3_$c.ko $A/built/qca_cld3_$c.ko $A/dump/$c
if [ -z "${KEEP:-}" ]; then D=$W/bazel-out/k8-fastbuild/bin/re/qcawlan_${TAG:-g40d}; chmod -R u+w $D 2>/dev/null; rm -rf $D; echo "bazel-out removed"; fi
df -h / | tail -1
