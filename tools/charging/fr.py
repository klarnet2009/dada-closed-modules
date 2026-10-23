#!/usr/bin/env python3
"""fr.py <dis.txt> <func> [lo hi]: compact listing of one function from an llvm-objdump -d -r dump, function-relative hex offsets,
relocations inlined as [SYM], adrp/add pairs shortened. local use: python fr.py ../g14f_tools/dump_X/dis.txt func 0x3c0 0x600"""
import sys, re
dis, fn = sys.argv[1], sys.argv[2]
lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else 1 << 30
base = None; cur = None; out = []; inf = False
for l in open(dis, encoding='latin1'):
    l = l.rstrip('\n')
    m = re.match(r'^([0-9a-f]+) <(.+)>:', l)
    if m:
        inf = (m.group(2) == fn)
        if inf: base = int(m.group(1), 16)
        continue
    if not inf or not l.strip(): continue
    m = re.match(r'^\s*([0-9a-f]+):\s+(R_AARCH64_\w+)\s+(.*)$', l)
    if m:
        if cur is not None and lo <= cur < hi:
            t = m.group(2).replace('R_AARCH64_', '')
            if t.startswith('ADR_PREL_PG') or t.startswith('ADD_ABS'):
                if t.startswith('ADR'): out[-1] += '   ;' + m.group(3)
            else:
                out[-1] += '   ;' + t + ' ' + m.group(3)
        continue
    m = re.match(r'^\s*([0-9a-f]+):\s+(.*)$', l)
    if not m: continue
    cur = int(m.group(1), 16) - base
    ins = re.sub(r'\s+', ' ', re.sub(r'//.*$', '', m.group(2))).strip()
    ins = re.sub(r' <[^>]*>', '', ins)
    ins = re.sub(r'^(b\.?\w*|cbn?z|tbn?z) (.*?)0x([0-9a-f]+)$', lambda k: '%s %s@%x' % (k.group(1), k.group(2), int(k.group(3), 16) - base), ins)
    if lo <= cur < hi: out.append('%x %s' % (cur, ins))
print('\n'.join(out))
