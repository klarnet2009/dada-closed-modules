"""enumchk.py <dump_dir> <stubs_include_dir> : compare the module-local BTF enums with the enumerators defined in the headers"""
import re, sys, glob, os
d, inc = sys.argv[1:3]
t = open(d + '/btf_all.txt').read()
hdr = ''
for f in glob.glob(inc + '/**/*.h', recursive=True):
    hdr += open(f, errors='replace').read() + '\n'
# crude: name -> value from "NAME = N" or sequential
defs = {}
for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9a-fA-F]+|-?\d+)', hdr):
    defs.setdefault(m.group(1), set()).add(int(m.group(2), 0))
present = set(re.findall(r'\b[A-Za-z_][A-Za-z0-9_]*\b', hdr))
for m in re.finditer(r'^enum (\w+) \{(.*?)\};', t, re.M):
    name = m.group(1)
    items = [x.strip() for x in m.group(2).split(',') if x.strip()]
    miss = []; bad = []
    for it in items:
        n, v = [p.strip() for p in it.split('=')]
        v = int(v, 0)
        if n not in present: miss.append(n)
        elif n in defs and v not in defs[n]: bad.append('%s(btf %d, hdr %s)' % (n, v, sorted(defs[n])))
    if miss or bad:
        print('enum %s: %d items, MISSING %d: %s | MISMATCH: %s' % (name, len(items), len(miss), ' '.join(miss)[:300], ' '.join(bad)[:300]))
