#!/usr/bin/env bash
# fd17.sh <mod> <func> [lines]: side-by-side-ish normalized disassembly diff stock vs built
T=<work-dir> O=<build-dir>
for w in stock built; do $O -dr --no-show-raw-insn $T/$w/$1.ko | awk -v f="$2" '$0 ~ "^[0-9a-f]+ <"f">:$"{p=1;next} /^[0-9a-f]+ <.*>:$/{p=0} p' | sed -E 's/^ +[0-9a-f]+:\s+//; s/^\t+[0-9a-f]+:\s+R_AARCH64_/  R_/; s/0x[0-9a-f]+ <[^>]*>/L/; s/\.bss\.[A-Za-z_0-9.]+/.bss/' > /tmp/g20_$1_$2_$w.txt; done
diff /tmp/g20_$1_$2_stock.txt /tmp/g20_$1_$2_built.txt | head -${3:-120}
