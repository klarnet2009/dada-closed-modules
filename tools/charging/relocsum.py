"""relocsum.py <compare_ko_v2 -v output>: per differing function, the call targets (ext:/fn) only in stock / only in built"""
import sys, re
for l in open(sys.argv[1], errors='replace'):
    l = l.rstrip()
    m = re.match(r"\s+(\S+): stock-only \[(.*?)\] \| built-only \[(.*)\]", l)
    if not m:
        continue
    f, a, b = m.groups()
    ea = re.findall(r"'(ext:[^']+)'", a)
    eb = re.findall(r"'(ext:[^']+)'", b)
    if ea or eb:
        print(f, '| stock-only:', ea, '| built-only:', eb)
