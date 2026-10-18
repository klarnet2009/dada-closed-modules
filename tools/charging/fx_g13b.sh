#!/bin/bash
# fx.sh <func> [from_hex] [to_hex]: compact stock listing (no prologue/epilogue pairs)
python3 "$(dirname $0)/fn.py" "$(dirname $0)/${DUMP:-dump_mca_strategy_fg_comp}" $1 2>&1 | sed 's/  */ /g; s/^ *//' | grep -v -E "^[0-9a-f]+: (paciasp|autiasp|ret)|^[0-9a-f]+: (ldp|stp) x(19|2[0-9]|29), x(2[0-9]|30), \[sp" | awk -v f=${2:-0} -v t=${3:-fffff} 'BEGIN{F=strtonum("0x" f);T=strtonum("0x" t)} {split($1,a,":"); o=strtonum("0x" a[1]); if(o>=F && o<=T) print}'
