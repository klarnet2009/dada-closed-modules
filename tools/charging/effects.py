#!/usr/bin/env python3
"""effects.py <X_stock> <X_ours> : multiset of CALL (name + constant args) and ST (target field + constant value) lines; prints one-sided entries."""
import re, sys, collections
def load(p):
    c = collections.Counter()
    for l in open(p, encoding='utf-8'):
        l = l.rstrip('\n')
        m = re.match(r'^  (CALL|TAILCALL) (\S+?)\((.*)\)$', l)
        if m:
            args = [a for a in re.split(r', (?=a\d=)', m.group(3))]
            keep = [re.sub(r'^a\d=', '', a) for a in args if re.match(r'a\d=("[^"]*"|-?\d+|0x[0-9a-f]+)$', a)]
            nm = re.sub(r'^\.text\+0x[0-9a-f]+$', '.text+X', m.group(2))
            c['CALL %s %s' % (nm, ' '.join(keep))] += 1
            continue
        m = re.match(r'^  ST (u8 |u16 )?(\S+) = (-?\d+|0x[0-9a-f]+)$', l)
        if m: c['ST %s%s = %s' % (m.group(1) or '', re.sub(r'@\.bss\+0x[0-9a-f]+|@update_batt_para', '@static', m.group(2)), m.group(3))] += 1
    return c
S, O = load(sys.argv[1]), load(sys.argv[2])
n = 0
for k in sorted(set(S) | set(O)):
    if S[k] != O[k]:
        n += 1; print('S%d O%d %s' % (S[k], O[k], k[:200]))
print('# entries stock %d ours %d, differing keys %d' % (sum(S.values()), sum(O.values()), n))
