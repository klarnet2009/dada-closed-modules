"""rel_diff.py <stock.ko> <built.ko> <funcs.txt|-> : per function, multiset diff of relocation targets (called functions fn:, strings str:,
objects) of stock vs built; offsets inside the target dropped. Output: name  then '-' lines (more in stock) and '+' lines (more in ours).
Skips noise: section-relative .text/.rodata.str targets are resolved by compare_ko.Elf.target to str:<text>/fn:<name>."""
import collections, re, sys
sys.path.insert(0, '<work-dir>')
from fcmp import funcs

stock, built, fl = sys.argv[1:4]
want = None if fl == '-' else set(open(fl).read().split())
fa, fb = funcs(stock), funcs(built)


def norm(t):
    t = re.sub(r'\+\d+$', '', t) if t.startswith(('fn:', 'ext:')) else t
    t = re.sub(r'\.llvm\.\d+', '', t)
    return t


n_same = 0
for f in sorted(fa):
    if want is not None and f not in want:
        continue
    if f not in fb:
        print(f, 'MISSING')
        continue
    ca = collections.Counter(norm(r[2]) for r in fa[f][1])
    cb = collections.Counter(norm(r[2]) for r in fb[f][1])
    d = [(k, cb[k] - ca[k]) for k in sorted(set(ca) | set(cb)) if ca[k] != cb[k]]
    if not d:
        n_same += 1
        if want is not None:
            print(f, 'RELSAME')
        continue
    print(f, 'RELDIFF')
    for k, v in d:
        print('    %+d %s' % (v, k[:150]))
print('relsame', n_same, file=sys.stderr)
