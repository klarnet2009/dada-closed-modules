#!/usr/bin/env bash
# ofn.sh <module> <func> [maxlines]: OUR built object (ThinLTO emulation) disassembly, compact (like fn.sh); whole-object dump + awk by label
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
ssh $K $U "cd <work-dir> && <build-dir> -dr --no-show-raw-insn o/$1.lto.o > o/$1.dis.txt; awk -v f='<$2>:' '\$2==f{p=1;print;next} p&&/^[0-9a-f]+ </{exit} p' o/$1.dis.txt | sed -E 's/^\s+([0-9a-f]+):\s+/\1: /; s/^\t+[0-9a-f]+:\s+/    ;; /; s/R_AARCH64_//; s/^[0-9a-f]{16}: /   ;; /; s/(b\.?[a-z]*|cbn?z|tbn?z)(\s.*)0x[0-9a-f]+ <[^+>]*\+?(0x[0-9a-f]+)?>/\1\2L\3/; s/0x[0-9a-f]+ <[^>]*>/@/' | grep -vE '^[0-9a-f]+: ([0-9a-f]{2} ){4}' | head -${3:-500}"
