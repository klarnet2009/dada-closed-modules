#!/usr/bin/env python3
"""G40a: diff two kernel .config/ikconfig files; 'is not set' == missing == n.
cfgdiff.py <stock_cfg> <ours_cfg>   -> lines 'CONFIG_X stock -> ours'
"""
import re, sys


def load(p):
    d = {}
    for l in open(p, errors='replace'):
        m = re.match(r'(CONFIG_\w+)=(.*)', l) or re.match(r'# (CONFIG_\w+) is not set', l)
        if m:
            d[m.group(1)] = m.group(2) if m.lastindex == 2 else 'n'
    return d


s, o = load(sys.argv[1]), load(sys.argv[2])
for k in sorted(set(s) | set(o)):
    a, b = s.get(k, 'n'), o.get(k, 'n')
    if a != b:
        print('%s %s -> %s' % (k, a, b))
