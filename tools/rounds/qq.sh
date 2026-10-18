#!/usr/bin/env bash
# qq.sh <module> [filter-regex]: q.sh but stops on compile errors; drops RELOC/add-lo12 noise from diffs
cd "$(dirname "$0")/.." || exit 1
out=$(bash g11b_tools/q.sh "$1" 2>&1)
if echo "$out" | grep -qE "error:|ld.lld: error"; then echo "$out" | grep -E "error|note:" | head -${ERRN:-15}; exit 1; fi
echo "$out" | grep -vE "^(RELOC|add\s+x[0-9]+, x[0-9]+, #0x0)" | head -${HEADN:-80}
