#!/usr/bin/env bash
# fd2.sh MOD func [ctx]: on server, normalised unified diff stock vs built for one function (branch targets -> L, addresses stripped, relocs dropped to names)
MOD=$1; F=$2; C=${3:-3}; D=<work-dir>
B=<build-dir>
norm(){ $B -dr --no-show-raw-insn $1 | awk -v f="$F" '$0 ~ ("^[0-9a-f]+ <" f ">:"){p=1;next} /^[0-9a-f]+ <.*>:/{p=0} p' | sed -E 's/^	+[0-9a-f]+: +R_AARCH64_(ADR|ADD|LDST|LD)[A-Z0-9_]*	.*$/SKIP/; s/^ +[0-9a-f]+: +/@ /; s/^\t+[0-9a-f]+: +R_AARCH64_[A-Z0-9_]+\t+/   REL /; s/0x[0-9a-f]+ <[^>]*>/L/; s/\.llvm\.[0-9]+//; s/\.rodata\.str1\.1/.rodata/' | awk '/^SKIP/{next} /^@/{if(b!="")print b; b=$0; next} /REL/{b=b" ;"$2; next} END{print b}' ; }
norm $D/stock/$MOD.ko > $D/.s.$$; norm $D/built/$MOD.ko > $D/.b.$$
diff -U$C $D/.s.$$ $D/.b.$$ | cut -c1-${CW:-170} | head -${N:-150}
rm -f $D/.s.$$ $D/.b.$$
