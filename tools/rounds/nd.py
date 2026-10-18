#!/usr/bin/env python3
# nd.py <objdump -dr file> : one line per insn, relocation symbol appended, branch targets as offsets
import re,sys
out=[];
for l in open(sys.argv[1],errors='replace'):
    l=l.rstrip('\n')
    m=re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s*(.*)$',l)
    if m:
        out.append([m.group(1),m.group(2)]);continue
    m=re.match(r'^\s*([0-9a-f]+):\s+(\S+)\s+(\S+)\s*$',l)
    if m and m.group(2).startswith('R_AARCH64') and out:
        out[-1][1]+='  @'+m.group(3).replace('R_AARCH64_','')+':'+m.group(2).replace('R_AARCH64_','');continue
    m=re.match(r'^\s*([0-9a-f]+):\s+(.*)$',l)
    if m and not m.group(2).startswith('R_AARCH64'):
        out.append([m.group(1),m.group(2)])
    elif m and out:
        out[-1][1]+='  @'+m.group(2)
for a,i in out:
    i=re.sub(r'\s+',' ',i)
    if '  @' in i and not re.match(r'(bl?|b\.\w+|cbn?z|tbn?z) ',i): i=re.sub(r'0x[0-9a-f]+ <[^>]*>','',i)
    i=re.sub(r'0x[0-9a-f]+ <([^>+]*)\+?(0x[0-9a-f]+)?>',lambda m:'L'+(m.group(2) or '0') if m.group(1).startswith(sys.argv[2] if len(sys.argv)>2 else 'zz') else '@'+m.group(1),i)
    print(a+': '+i)
