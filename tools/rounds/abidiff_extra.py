#!/usr/bin/env python3
"""G40a: BTF checks beyond named structs (stock vs ours):
 - enums (incl. anonymous, keyed by first enumerator): name->value changes
 - typedef'd anonymous structs/unions (typedef struct {..} x_t)
 - tracepoint / vendor-hook probe prototypes (typedef btf_trace_<tp>)
 - FUNC prototypes by name (optionally only symbols from a symvers file)
abidiff_extra.py <stock.btf> <ours.btf> [symvers]  -> stdout report
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from btfparse import BTF


def enums(b):
    d = {}
    for i in range(1, len(b.types)):
        k, n, st, kf, vl, ex = b.types[i]
        if k == 'enum' and ex:
            vals = tuple((b.s(no), v) for no, v in ex)
        elif k == 'enum64' and ex:
            vals = tuple((b.s(no), lo | (hi << 32)) for no, lo, hi in ex)
        else:
            continue
        key = 'enum ' + (n or '{%s..}' % vals[0][0])
        d.setdefault(key, set()).add(vals)
    return d


def anon_typedefs(b):
    d = {}
    for i in range(1, len(b.types)):
        k, n, st = b.types[i][:3]
        if k == 'typedef':
            t = b.strip(st)
            if b.types[t][0] in ('struct', 'union') and not b.types[t][1]:
                d.setdefault(n, set()).add((b.types[t][2], tuple(b.flat(t))))
    return d


def protos(b, pred):
    d = {}
    for i in range(1, len(b.types)):
        k, n, st = b.types[i][:3]
        if pred(k, n):
            d.setdefault(n, set()).add(b.tstr(st))
    return d


def cmp(title, a, b, show=lambda x: str(x)):
    out = []
    for k in sorted(set(a) & set(b)):
        if a[k] != b[k]:
            out.append(k)
    print('### %s: %d differ (common %d, only_stock %d, only_ours %d)' % (
        title, len(out), len(set(a) & set(b)), len(set(a) - set(b)), len(set(b) - set(a))))
    return out


def main():
    bs, bo = BTF.load(sys.argv[1]), BTF.load(sys.argv[2])
    exp = None
    if len(sys.argv) > 3:
        exp = {l.split('\t')[1] for l in open(sys.argv[3]) if '\t' in l}
    es, eo = enums(bs), enums(bo)
    for k in cmp('enums', es, eo):
        sa, sb = es[k], eo[k]
        if len(sa) == 1 and len(sb) == 1:
            da, db = dict(next(iter(sa))), dict(next(iter(sb)))
            ch = ['%s:%s->%s' % (n, da.get(n), db.get(n)) for n in list(da) + [n for n in db if n not in da] if da.get(n) != db.get(n)]
            print('  %s: %s' % (k, ' '.join(ch[:12]) + (' ...(+%d)' % (len(ch) - 12) if len(ch) > 12 else '')))
        else:
            print('  %s: %d vs %d variants' % (k, len(sa), len(sb)))
    ts, to = anon_typedefs(bs), anon_typedefs(bo)
    for k in cmp('typedef anon struct', ts, to):
        print('  %s: stock %s | ours %s' % (k, sorted(ts[k])[:1], sorted(to[k])[:1]))
    tp = lambda k, n: k == 'typedef' and n.startswith('btf_trace_')
    ps, po = protos(bs, tp), protos(bo, tp)
    for k in cmp('tracepoint protos (btf_trace_*)', ps, po):
        print('  %s:\n     stock %s\n     ours  %s' % (k, sorted(ps[k]), sorted(po[k])))
    fp = lambda k, n: k == 'func'
    fs, fo = protos(bs, fp), protos(bo, fp)
    d = cmp('FUNC protos (all)', fs, fo)
    if exp is not None:
        d = [k for k in d if k in exp]
        print('  of which exported (in symvers): %d' % len(d))
    for k in d[:200]:
        print('  %s:\n     stock %s\n     ours  %s' % (k, sorted(fs[k]), sorted(fo[k])))


main()
