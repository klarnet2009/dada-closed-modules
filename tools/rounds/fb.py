"""fb.py <stock.ko> <built.ko> [func...] : compare_ko_v2 verdict per function (bytes = code words, relocs = releq); no names: list the differing ones"""
import sys
sys.path.insert(0, '<work-dir>')
import compare_ko_v2 as C
a = C.Elf(sys.argv[1]).functions(); b = C.Elf(sys.argv[2]).functions()
want = sys.argv[3:]
bad = []
for f in sorted(a):
    if f not in b: bad.append((f, 'MISSING')); continue
    cb = a[f][0] != b[f][0]
    rb = not C.releq([x for x in a[f][1]], [x for x in b[f][1]])
    if cb or rb: bad.append((f, ('bytes ' if cb else '') + ('relocs' if rb else '')))
if want:
    d = dict(bad)
    for f in want: print('%-6s %s %s' % ('DIFF' if f in d else 'SAME', f, d.get(f, '')))
else:
    print(len(bad), 'functions differ')
    for f, w in bad: print(' ', f, w)
