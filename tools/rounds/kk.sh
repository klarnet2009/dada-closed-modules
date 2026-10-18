#!/usr/bin/env bash
# kk.sh <module> [funcs...]: real Kleaf build (kb.sh), print compile errors + summary of compare_ko_v2, then opb multiset diff of the named functions (V=1 lines)
cd "$(dirname "$0")/.." || exit 1
m=$1; shift
out=$(N=60 bash g1q2_tools/kb.sh $m 2>&1)
echo "$out" | grep -E "error|ERROR|NO KO|^(FAIL|PASS) (function|kcfi|imports|exports)|VERDICT|preamble mism" | cut -c1-220
MIN=${MIN:-1} V=${V:-1} MOD=$m bash g1q2_tools/ob.sh <work-dir> "$@" | cut -c1-160
