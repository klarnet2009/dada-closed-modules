#!/usr/bin/env bash
# pre.sh <module> [built]: kCFI preamble word compare stock vs built (emulation .lto.o by default): lists every mismatch
D="$(dirname "$0")"; m=$1; b=${2:-<work-dir>
bash $D/sv.sh "cd <work-dir> && python3 - <<'P'
import sys
sys.path.insert(0,'.')
from verify_pkg import Ko, preambles
a=preambles(Ko('<work-dir>')); b=preambles(Ko('$b'))
com=[n for n in a if n in b]
bad=[n for n in com if a[n]!=b[n]]
print('common',len(com),'mismatch',len(bad),'only stock',[n for n in a if n not in b][:40],'only built',[n for n in b if n not in a][:40])
for n in bad: print('  %s stock %#x built %#x'%(n,a[n],b[n]))
P"
