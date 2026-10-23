#!/usr/bin/env python3
"""votes.py <objdump -dr text> : for every `bl mca_vote` (relocation CALL26 mca_vote) print
function, the voter slot (x0 source), the string reloc (x1), and the constants w2 (enable) / w3 (value).
Heuristic backward scan (since the previous call) over the straight-line code; csel/csinc selections are printed as text."""
import re, sys, collections, os
VF=os.environ.get('VF','mca_vote')

fn = None
ins = []          # (addr, text)
rel = {}          # addr -> reloc text
rows = []
cur_fn = None
lines = open(sys.argv[1], encoding='utf-8', errors='replace').read().split('\n')
i = 0
funcs = collections.OrderedDict()
for l in lines:
    m = re.match(r'^[0-9a-f]+ <(\S+)>:', l)
    if m:
        cur_fn = m.group(1); funcs[cur_fn] = []; continue
    if cur_fn is None:
        continue
    m = re.match(r'^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2}\s+)?(\S.*)$', l)
    if m and not re.match(r'^\s+[0-9a-f]+:\s+R_', l):
        funcs[cur_fn].append([m.group(1), re.sub(r'\s*//.*$', '', m.group(2)).strip(), None])
        continue
    m = re.match(r'^\s+[0-9a-f]+:\s+(R_AARCH64_\S+)\s+(\S+)', l)
    if m and funcs[cur_fn]:
        funcs[cur_fn][-1][2] = m.group(1) + ' ' + m.group(2)

for f, code in funcs.items():
    for k, (a, t, r) in enumerate(code):
        if re.match(r'^bl?\s',t) and r and r.endswith(' '+VF):
            info = {}
            j = k - 1
            while j >= 0 and not code[j][1].startswith('bl') and k - j < 40:
                tt, rr = code[j][1], code[j][2]
                mm = re.match(r'(\w+)\s+(x0|x1|w2|w3)\b,?\s*(.*)$', tt)
                if mm and mm.group(2) not in info:
                    info[mm.group(2)] = (mm.group(1) + ' ' + mm.group(3), rr)
                j -= 1
            def g(key):
                v = info.get(key)
                return '?' if v is None else (v[0] + (' [' + v[1] + ']' if v[1] else ''))
            rows.append((f, a, g('x0'), g('x1'), g('w2'), g('w3')))
for r in rows:
    print('%s +%s | x0: %s | x1: %s | w2: %s | w3: %s' % r)
