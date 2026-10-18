"""ndiff.py <stock.dis> <built.dis> [ctx]: diff of two dis.py outputs with branch labels normalised (L<hex> -> L), so shifted offsets do not matter."""
import re, sys, difflib
ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 2


def norm(path):
    out = []
    for l in open(path, errors='replace'):
        l = l.rstrip('\n')
        l = re.sub(r'^L[0-9a-f]+:\s*', '', l)
        l = re.sub(r'\bL[0-9a-f]+\b', 'L', l)
        l = re.sub(r'0x[0-9a-f]+ <[^>]*>', 'X', l)
        l = re.sub(r'"[^"]*"', 'S', l)
        if re.match(r'^[0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2}\s+\.word', l): continue
        l = re.sub(r'\.text[.+]\S+', '.text', l)
        l = re.sub(r'(g_draw_grp)\+\d+', r'\1+N', l) if '--nog' in sys.argv else l
        out.append(l.strip())
    return out


a, b = norm(sys.argv[1]), norm(sys.argv[2])
sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
n = 0
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag == 'equal':
        continue
    n += 1
    print(f'@@ stock {i1+1}-{i2} built {j1+1}-{j2} {tag}')
    for k in range(max(0, i1 - ctx), i1):
        print('  ', a[k])
    for k in range(i1, i2):
        print('-', a[k])
    for k in range(j1, j2):
        print('+', b[k])
    for k in range(i2, min(len(a), i2 + ctx)):
        print('  ', a[k])
print(f'# {n} hunks, ratio {sm.ratio():.4f}')
