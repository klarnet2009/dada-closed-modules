#!/usr/bin/env bash
# dfn.sh <stock|built> <mod> <func>: one function (insns + reloc names) from a full llvm-objdump -dr dump (cached in g41a/o/<kind>_<mod>.dis; delete to refresh)
bash "$(dirname "$0")/sv.sh" "cd <work-dir> f=o/$1_$2.dis; [ -s $f ] && [ $f -nt $1/$2.ko ] || <build-dir> -dr --no-show-raw-insn $1/$2.ko > \$f; awk -v F='<$3>:' 'index(\$0,F){p=1;next} /^[0-9a-f]{16} </{p=0} p' \$f | sed -E 's/^ +//; s/R_AARCH64_//' | cut -c1-100"
