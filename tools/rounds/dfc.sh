#!/usr/bin/env bash
# dfc.sh <stock|built> <mod> <func> [from-addr-hex]: compact disassembly (no adrp/add pairs, 3 insns per line)
bash "$(dirname "$0")/dfn.sh" $1 $2 $3 2>&1 | grep -v "^bash\|ADD_ABS" | sed -E 's/^\s+[0-9a-f]+:\s+//; s/\s+/ /g; s/ <[^>]*>//; s/^([0-9a-f]+): /\1 /' | sed -n "/^${4:-[0-9a-f]\+} /,\$p" | awk '{printf "%s | ", $0} NR%3==0{print ""}' | cut -c1-250
