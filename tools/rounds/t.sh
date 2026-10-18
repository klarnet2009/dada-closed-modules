#!/usr/bin/env bash
# t.sh <module> [funcs...]: build emulation (stop on error), then multiset diff of the named functions (V=1 if set)
D="$(dirname "$0")"; m=$1; shift
out=$(TAILN=3000 bash $D/q.sh $m 2>&1)
if echo "$out" | grep -qE "error:|ld.lld: error"; then echo "$out" | grep -E "error|note:" | head -15; exit 1; fi
MOD=$m MIN=${MIN:-1} V=${V:-} bash $D/ob.sh "$@" 2>&1 | head -${HEADN:-60}
