#!/usr/bin/env bash
# line_var.sh <file> <lineno> <func> <variant text...>: temporarily replace one line of <file>, run kk.sh on its module, show opb of <func>, restore
f=$1; ln=$2; fn=$3; shift 3
cp "$f" /tmp/line_var.bak
mod=$(echo "$f" | cut -d/ -f1)
for v in "$@"; do
  awk -v n="$ln" -v t="$v" 'NR==n{print t; next}{print}' /tmp/line_var.bak > "$f"
  echo "## variant: $v"
  bash g1q2_tools/kk.sh $mod $fn 2>&1 | grep -v "^FAIL\|^PASS\|VERDICT\|preamble" | cut -c1-120
done
cp /tmp/line_var.bak "$f"
