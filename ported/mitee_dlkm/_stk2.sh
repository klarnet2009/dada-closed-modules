#!/usr/bin/env bash
# _stk2.sh <func> [emu]: compact listing: relocs inlined on the call line, strings resolved
B=<build-dir>
F=${2:+built/mitee_emu.ko}; F=${F:-stock/mitee_dlkm.ko}
ssh -i "<ssh-key-file>" -o BatchMode=yes <user>@<build-server> "cd <work-dir> && $B -dr --no-show-raw-insn $F | awk '/^[0-9a-f]+ <$1(\.llvm\.[0-9]+)?>:/{p=1} p&&/^\$/{exit} p' | cut -c1-140" | python3 -c "
import sys,re
L=sys.stdin.read().split('\n')
out=[]
for l in L:
    m=re.match(r'^\s+[0-9a-f]+:\s+R_AARCH64_(\w+)\s+(\S+)',l.strip()) if False else None
    s=l.strip()
    m=re.match(r'^[0-9a-f]{16}:\s+R_AARCH64_(\w+)\s+(\S+)',s)
    if m:
        if out and m.group(1) in ('CALL26','JUMP26','ADD_ABS_LO12_NC','LDST64_ABS_LO12_NC','LDST32_ABS_LO12_NC','LDST8_ABS_LO12_NC','LDST16_ABS_LO12_NC') : out[-1]+='   ;; '+m.group(2)
        continue
    s=re.sub(r'<[^>]*>','',s)
    out.append(s)
print('\n'.join(out))
"
