import sys
sys.path.insert(0, '<work-dir>')
import pathcmp as P
ko, fname, filt = sys.argv[1], sys.argv[2], sys.argv[3]
e = P.Elf(ko)
rs = P.listing(ko, fname, e)
ar = {}; P.arity_pass(rs, ar)
f = P.Fn2(rs, ar)
R, A = P.collect(f)
for k in sorted(R, key=lambda k: A[k]):
    if filt in P.fmt(k):
        print('%x' % A[k], P.fmt(k)[:150], ' REACH:', P.short(R[k])[:300])
