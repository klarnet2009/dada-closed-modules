#!/usr/bin/env bash
# tryfile.sh <module> <variant.c> <func> : run q.sh with <variant.c> in place of <module>/<module>.c, restore afterwards; prints the function summary
cd "$(dirname "$0")/.." || exit 1
m=$1; v=$2; fn=$3; f=$m/$m.c
cp $f g14i_tools/.orig_$m.c
sed 's/$/\r/' "$v" | sed 's/\r\r$/\r/' > $f
out=$(FD=$fn HEADN=${HEADN:-400} bash g14i_tools/qq.sh $m 2>&1)
cp g14i_tools/.orig_$m.c $f
echo "$out" | grep -E "error|^===== |^$fn " | head -${N:-5}
[ -n "$SHOW" ] && echo "$out" | sed -n "/^===== /,\$p" | head -${SHOW}
