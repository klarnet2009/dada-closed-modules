#!/usr/bin/env bash
# rawf.sh <stock|built> <mod> <func>: objdump of one function with relocation targets appended to the instruction line, offsets relative to function start
w=$1; m=$2; f=$3
bash "$(dirname "$0")/sv.sh" "cd <work-dir> && B=<build-dir> \$B -dr --no-show-raw-insn $w/$m.ko | awk '/^[0-9a-f]+ <$f>:\$/{p=1;next} /^\$/{p=0} p'" | python3 -c "
import sys,re
lines=sys.stdin.read().split('\n'); out=[]; base=None
for l in lines:
    m=re.match(r'^\s+([0-9a-f]+):\s+(.*)$',l)
    if m and not l.strip().split()[1].startswith('R_AARCH64'):
        a=int(m.group(1),16)
        if base is None: base=a
        out.append(['%04x'%(a-base), re.sub(r'\s+',' ',re.sub(r'<[^>]*>','',m.group(2)))])
    else:
        r=re.search(r'R_AARCH64_(\w+)\s+(\S+)',l)
        if r and out: out[-1][1]+='   ;'+r.group(2)
print('\n'.join(o[0]+': '+o[1] for o in out))
"
