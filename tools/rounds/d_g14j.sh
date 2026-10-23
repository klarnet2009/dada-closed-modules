#!/usr/bin/env bash
# d.sh <module> <func> [nlines]: ThinLTO emulation diff of one function, compact (reg-rename noise kept; use ctx lines)
cd "$(dirname "$0")/.." || exit 1
FD="$2" HEADN=3000 bash g14j_tools/qq.sh $1 2>&1 | sed -n "/^===== $2 /,\$p" | head -${3:-120} | cut -c1-160
