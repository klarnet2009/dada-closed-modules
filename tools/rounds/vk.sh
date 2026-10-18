#!/usr/bin/env bash
# vk.sh <module> <func> [ctx] [N]: normalised diff stock vs the LAST real Kleaf build (<work-dir> run kk.sh first)
cd "$(dirname "$0")/.." || exit 1
m=$1; f=$2; c=${3:-1}; n=${4:-80}
S=/tmp/g1q2_stock_${m}_${f}.dis
[ -s "$S" ] || bash g1q2_tools/sv.sh "cd <work-dir> && python3 dis.py $m $f stock" > "$S" 2>&1
bash g1q2_tools/sv.sh "cd <work-dir> && python3 dis.py $m $f built" > /tmp/g1q2_built_${m}_${f}.dis 2>&1
echo "stock $(wc -l < $S) lines, built $(wc -l < /tmp/g1q2_built_${m}_${f}.dis) lines"
python3 g1q2_tools/ndiff.py "$S" /tmp/g1q2_built_${m}_${f}.dis $c | head -$n
