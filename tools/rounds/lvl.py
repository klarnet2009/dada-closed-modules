"""lvl.py stock.ko built.ko: printk format strings whose level (or text) differs between stock and built"""
import re, sys, collections
def strs(p):
    d = open(p, 'rb').read()
    r = collections.defaultdict(list)
    for m in re.finditer(rb'\x01([0-7])([^\x00]{1,300})\x00', d):
        r[m.group(2)].append(m.group(1).decode())
    return r
a, b = strs(sys.argv[1]), strs(sys.argv[2])
lv = collections.Counter()
for t in sorted(set(a) | set(b)):
    x, y = sorted(a.get(t, [])), sorted(b.get(t, []))
    if x != y:
        if t in a and t in b:
            print('LEVEL stock=%s built=%s %r' % (''.join(x), ''.join(y), t.decode('latin1')[:90]))
            lv[(tuple(x), tuple(y))] += 1
        elif t in a:
            print('STOCK-ONLY %s %r' % (''.join(x), t.decode('latin1')[:90]))
        else:
            print('BUILT-ONLY %s %r' % (''.join(y), t.decode('latin1')[:90]))
print(lv)
