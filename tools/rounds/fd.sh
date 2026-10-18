#!/usr/bin/env bash
# fd.sh <module> <func> [lines]: stock disassembly of one function (server llvm-objdump)
B=<build-dir>
bash "$(dirname "$0")/sv.sh" "$B/llvm-objdump -dr --no-show-raw-insn <work-dir> 2>/dev/null | awk -v f='<$2>:' '\$2==f{p=1;print;next} p&&/^[0-9a-f]+ <.*>:\$/{exit} p' | head -${3:-60}"
