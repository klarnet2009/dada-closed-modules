"""ldiff.py <stock.lst> <ours.lst> [ctx]: diff two lst2.py listings (addresses, branch targets, adrp page and .bss/.data/.rodata names masked)"""
import sys, re, difflib


def load(p):
    r = []
    for l in open(p, encoding='utf-8', errors='replace'):
        m = re.match(r'\s*([0-9a-f]+): (.*)$', l.rstrip('\n'))
        if not m:
            continue
        t = re.sub(r'@[0-9a-f]+', '@', m.group(2))
        t = re.sub(r'^(adrp x\d+), (@|0x[0-9a-f]+)', lambda mm: mm.group(1), t)
        t = re.sub(r'\.(bss|data|rodata)[.+]\S*', '.SEC', t)
        t = re.sub(r'\s+', ' ', t).strip()
        if t.startswith('paciasp'):
            t = 'paciasp'
        r.append((m.group(1), t))
    return r


a = load(sys.argv[1])
b = load(sys.argv[2])
sm = difflib.SequenceMatcher(None, [x[1] for x in a], [x[1] for x in b], autojunk=False)
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag == 'equal':
        continue
    print('@@ stock %s..%s  ours %s..%s' % (a[i1][0] if i1 < len(a) else 'end', a[i2 - 1][0] if i2 > i1 else '-',
                                           b[j1][0] if j1 < len(b) else 'end', b[j2 - 1][0] if j2 > j1 else '-'))
    for k in range(i1, i2):
        print('-', a[k][1])
    for k in range(j1, j2):
        print('+', b[k][1])
