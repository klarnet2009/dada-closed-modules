#!/usr/bin/env python3
"""atoms.py <X_stock.txt> <X_ours.txt> : multiset diff of canonical IF-atoms of two sx.py outputs (polarity-free, operands normalised).
Differences are candidates to read by hand; equal multisets do NOT prove equality."""
import re, sys, collections
def canon(c):
    c = re.sub(r'φ\d+', 'PHI', c); c = re.sub(r'\bt\d+\b', 'T', c); c = re.sub(r'ret\d+:', 'ret:', c)
    c = re.sub(r'\bx\d+@in', 'XIN', c)
    c = c.strip()
    while c.startswith('(') and c.endswith(')') and c.count('(') == c.count(')') and re.fullmatch(r'\(.*\)', c) and c[1:-1].count('(') == c[1:-1].count(')'):
        c = c[1:-1]
    m = re.fullmatch(r'(.*?) (==|!=|<=u|>=u|<u|>u|<=|>=|<|>) (.*)', c)
    if not m: return c
    a, op, b = m.groups()
    if op in ('!=',): op = '=='
    elif op == '>=': op, a, b = '<', a, b
    elif op == '>': op, a, b = '<', b, a
    elif op == '<=': op, a, b = '<', b, a
    elif op == '>=u': op = '<u'
    elif op == '>u': op, a, b = '<u', b, a
    elif op == '<=u': op, a, b = '<u', b, a
    if op == '==' and a > b: a, b = b, a
    return '%s %s %s' % (a, op, b)
def atoms(p):
    cnt = collections.Counter()
    for l in open(p, encoding='utf-8'):
        m = re.match(r'^  IF \((.*)\) -> B[0-9a-f]+ else B[0-9a-f]+$', l.rstrip('\n'))
        if m:
            c = m.group(1)
            # split top-level && / ||
            parts = re.split(r' && | \|\| ', c) if '?' not in c else [c]
            for q in parts: cnt[canon(q)] += 1
    return cnt
S, O = atoms(sys.argv[1]), atoms(sys.argv[2])
for k in sorted(set(S) | set(O)):
    if S[k] != O[k]:
        print('S%d O%d  %s' % (S[k], O[k], k[:230]))
print('# atoms stock %d ours %d' % (sum(S.values()), sum(O.values())))
