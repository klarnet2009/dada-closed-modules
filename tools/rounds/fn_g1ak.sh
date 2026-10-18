#!/usr/bin/env bash
# fn.sh <module> <func> [maxlines]: stock disassembly with relocs inline (compact); branch targets as L<offset>; instruction offsets as prefix
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
ssh $K $U "cd <work-dir> && awk -v f='<$2>:' '\$2==f{p=1;print;next} p&&/^[0-9a-f]+ </{exit} p' dis.txt | grep -v '^Disassembly' | sed -E 's/^\s+([0-9a-f]+):\s+/\1: /; s/^\t+[0-9a-f]+:\s+/    ;; /; s/R_AARCH64_//; s/^[0-9a-f]{16}: /   ;; /; s/(b\.?[a-z]*|cbn?z|tbn?z)(\s.*)0x[0-9a-f]+ <[^+>]*\+?(0x[0-9a-f]+)?>/\1\2L\3/; s/0x[0-9a-f]+ <[^>]*>/@/' | grep -vE '^[0-9a-f]+: ([0-9a-f]{2} ){4}' | head -${3:-500}"
