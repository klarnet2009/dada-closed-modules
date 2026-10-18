"""kcn.py <hash> <nargs> : struct-name dictionary search: void(void*, [struct X * at some pos], ints...)"""
import sys, itertools
from xxh import kh
want = {int(sys.argv[1], 16)}
names = [l.split()[1] for l in open('names.txt') if l.strip()] + ['mi_smart_refill_pool', 'mi_smart_refill_pool_stat', 'smart_refill_pool', 'mi_pool']
ints = ['m', 'i', 'j', 'l', 'y', 'b', 'Pv', 'x', 'h']
for X in set(names):
    px = 'P%d%s' % (len(X), X); pk = 'PK%d%s' % (len(X), X)
    for P in (px, pk):
        for k in range(1, 4):
            for pos in range(k + 1):
                for rest in itertools.product(ints, repeat=k):
                    seq = list(rest); seq.insert(pos, P)
                    for lead in ('Pv', ''):
                        m = '_ZTSFv' + lead + ''.join(seq) + 'E'
                        if kh(m) in want: print(m, X)
print('done')
