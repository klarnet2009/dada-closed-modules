#!/usr/bin/env bash
# fx.sh <stock|ours> <func> [maxlines]: compact disassembly with relocs (ours = ThinLTO emulation o/matt.lto.o, refreshed by `fx.sh ours -` )
D="$(dirname "$0")"
if [ "$1" = ours ] && [ "$2" = "-" ]; then bash $D/sv.sh "cd <work-dir> && <build-dir> -dr --no-show-raw-insn o/matt.lto.o > dump/matt/ours.txt && echo ok"; exit; fi
f=dis.txt; [ "$1" = ours ] && f=ours.txt
bash $D/sv.sh "cd <work-dir> && awk -v f='<$2>:' '\$2==f{p=1;print;next} p&&/^[0-9a-f]+ </{exit} p' $f | grep -v '^Disassembly' | sed -E 's/^\s+([0-9a-f]+):\s+/\1: /; s/^\t+[0-9a-f]+:\s+/    ;; /; s/R_AARCH64_//; s/^[0-9a-f]{16}: /   ;; /; s/(b\.?[a-z]*|cbn?z|tbn?z)(\s.*)0x[0-9a-f]+ <[^+>]*\+?(0x[0-9a-f]+)?>/\1\2L\3/; s/0x[0-9a-f]+ <[^>]*>/@/' | grep -vE '^[0-9a-f]+: ([0-9a-f]{2} ){4}' | head -${3:-500}"
