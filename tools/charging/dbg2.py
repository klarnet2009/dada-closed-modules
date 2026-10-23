import sys
sys.path.insert(0, '<work-dir>')
import pathcmp as P
ko, fname = sys.argv[1], sys.argv[2]
e = P.Elf(ko)
rs = P.listing(ko, fname, e)
ar = {}; P.arity_pass(rs, ar)
f = P.Fn2(rs, ar)
f.analyse()
for b, (s, en) in enumerate(f.blocks):
    a, m, ops, rel = rs[s]
    la = rs[en - 1]
    print('B%d @%x..%x R=%s preds=%s last=%s %s' % (b, a, rs[en-1][0], 'T' if f.R.get(b)==1 else ('0' if f.R.get(b)==0 else 'F%d' % f.R.get(b,-1)), [(p, k) for p, k in f.pred[b]], la[1], la[2]))
print('---')
import symcmp4 as S
for b in (4, 10, 11, 12, 13):
    st = f.ins[b]
    print('B%d in r20 =' % b, P.fmt(st.r.get(20, '?'))[:700])
print('--- expand')
v = f.ins[13].r[20]
for st, ff in P.expand(v):
    print(P.fmt(st)[:100], 'FORM=', ff)
print('--- inner')
import re
u = P._S.full(v)
m = P.TAGRE.search(u)
print('first tagged innermost:', m.group(0)[:300])
for it in P.split_top(m.group(1)):
    print('  item', it[:90], '|', it.rpartition('@')[2])
print('AND(59,28)=', P.AND(59, 28), 'AND(59,34)=', P.AND(59,34), 'R4=', f.R[4], 'R10=', f.R[10], 'R11=', f.R[11], 'R3', f.R[3], 'R8', f.R[8])
