#!/usr/bin/env bash
# od.sh <module> <func> [stock|ours]: normalised one-line-per-insn disassembly of a function (stock or our ThinLTO-emulation object), written to $S/<func>.{s,o}.n and printed with N lines from line F
R="$(cd "$(dirname "$0")/.." && pwd)"; S=${S:-<scratch>}; mkdir -p $S
B=<build-dir>
m=$1; f=$2
sv() { ssh -i "<ssh-key-file>" -o BatchMode=yes -o ConnectTimeout=20 <user>@<build-server> "$@"; }
sv "cd <work-dir> && $B -dr --no-show-raw-insn stock/$m.ko | awk -v f='<$f>:' '\$2==f{p=1;print;next} p&&/^[0-9a-f]+ <.*>:\$/{exit} p'" > $S/$f.s.raw
sv "cd <work-dir> && $B -dr --no-show-raw-insn o/${3:-$m}.lto.o | awk -v f='<$f>:' '\$2==f{p=1;print;next} p&&/^[0-9a-f]+ <.*>:\$/{exit} p'" > $S/$f.o.raw
for k in s o; do python "$R/g14d_tools/nd.py" $S/$f.$k.raw $f | sed -E 's/:ADR_PREL_PG_HI21//; s/:LDST[0-9]+_ABS_LO12_NC//; s/:ADD_ABS_LO12_NC//; s/@update_pkg_load //; s/\.llvm\.[0-9]+//; s/:CALL26//; s/@\.text\.[A-Za-z_0-9]+ //' > $S/$f.$k.n; done
wc -l $S/$f.s.n $S/$f.o.n
