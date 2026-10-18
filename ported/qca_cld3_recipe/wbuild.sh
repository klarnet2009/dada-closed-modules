#!/usr/bin/env bash
# wbuild.sh <bazel label> <stockname>: Kleaf build + compare vs OS4 stock (<work-dir>)
set -uo pipefail
L=$1; m=$2; W=<build-dir>; A=<work-dir>
[ "$(df --output=avail -BG / | tail -1 | tr -dc 0-9)" -ge 4 ] || { echo "ABORT: server disk <4G free"; exit 3; }
cd $W
BZ="tools/bazel --output_user_root=$W/.bazel_root"
nice -n 10 $BZ build --lto=none --config=stamp $L 2>&1 | grep -vE "warning: (ignoring|overriding)|^--|STABLE_SCMVERSIONS|^\[|^INFO: (Invocation|Analyzed|Found|From|Build option|Streaming)|^Loading|^Analyzing|^DEBUG" | grep -E "error|ERROR|Error|undefined|Build completed|fatal|no such|missing" | cut -c1-500 | head -${NL:-40}
P=$($BZ cquery --lto=none --config=stamp --output=files $L 2>/dev/null | head -1)
[ -n "$P" ] && [ -f "$P" ] || { echo "NO .ko PRODUCED"; exit 1; }
cp "$P" $A/built/$m.ko
python3 $A/scripts/compare_ko.py $A/stock/$m.ko $A/built/$m.ko "${@:3}"
