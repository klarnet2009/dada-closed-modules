#!/usr/bin/env python3
"""alpha3.py S.lst B.lst : block-wise alpha-renamed diff of two lst2 listings (G40e); normalises reloc names, adrp page targets; drops pure-reorder hunks."""
import re, sys, difflib
def load(p):
    rows = []
    for l in open(p, encoding='latin1'):
        m = re.match(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$', l.rstrip('\n'))
        if not m: continue
        rows.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
    tg = set()
    for o, mn, ops in rows:
        t = re.search(r'@([0-9a-f]+)', ops)
        if t and (mn in ('b', 'cbz', 'cbnz', 'tbz', 'tbnz') or mn.startswith('b.')): tg.add(int(t.group(1), 16))
    out, mp = [], {}
    def ren(mo):
        r = mo.group(0)
        if r in ('wzr', 'xzr', 'sp', 'x29', 'x30', 'w29'): return r
        k = r[1:]
        if k not in mp: mp[k] = 'r%d' % len(mp)
        return r[0] + mp[k][1:]
    prev_br = True
    for o, mn, ops in rows:
        if o in tg or prev_br: mp.clear()
        if mn == 'paciasp': ops = ''
        if mn == 'adrp': ops = re.sub(r',\s*@[0-9a-f]+', '', ops)
        ops = re.sub(r'@[0-9a-f]+', '@L', ops)
        ops = re.sub(r'\.text[.+][\w.+]*', 'LOCALFN', ops)
        ops = re.sub(r'\.(rodata|data|bss)[.+][\w.+]*', r'.\1', ops)
        ops = re.sub(r'\b[wx](\d+)\b', ren, ops)
        ops = re.sub(r'\s+', ' ', ops)
        mn = re.sub(r'^ldapur?(b|h)?$', lambda m: 'ldar' + (m.group(1) or ''), mn)
        mn = 'ldr' if mn == 'ldur' else mn
        out.append(('%s %s' % (mn, ops)).strip())
        prev_br = mn in ('b', 'ret', 'br', 'cbz', 'cbnz', 'tbz', 'tbnz') or mn.startswith('b.') or mn == 'bl'
    return rows, out
S, sn = load(sys.argv[1]); B, bn = load(sys.argv[2])
def off(R, i): return R[min(i, len(R) - 1)][0] if R else -1
sm = difflib.SequenceMatcher(None, sn, bn, autojunk=False)
eq = 0; nh = 0; ro = 0
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag == 'equal': eq += i2 - i1; continue
    if sorted(sn[i1:i2]) == sorted(bn[j1:j2]): ro += 1; continue
    nh += 1
    print('== %s S[%x..%x) B[%x..%x)' % (tag, off(S, i1), off(S, i2 - 1), off(B, j1), off(B, j2 - 1)))
    for k in range(i1, i2): print('  - %4x %s' % (S[k][0], sn[k]))
    for k in range(j1, j2): print('  + %4x %s' % (B[k][0], bn[k]))
print('# hunks %d reorder-only %d equal %d of stock %d ours %d' % (nh, ro, eq, len(sn), len(bn)))
