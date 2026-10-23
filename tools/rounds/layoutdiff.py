#!/usr/bin/env python3
"""G40a: compare all named struct/union layouts between two BTF blobs (stock vs ours).

layoutdiff.py <stock.btf> <ours.btf> <out_prefix>
 -> <out_prefix>.json  {name: {...}}   and <out_prefix>.txt (human summary)
A struct "differs" when the multiset of (size, flat member list) signatures differs.
"deep" = same own flat layout, but an embedded-by-value named struct differs.
"""
import json, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from btfparse import BTF


def sig(b, t):
    return (b.types[t][2], tuple(b.flat(t)))


def mdiff(fs, fo):
    """member-level diff of two flat lists -> list of (path, stock(off,bits,type)|None, ours(...)|None)."""
    ds, do = {p: (o, s, t) for p, o, s, t in fs}, {p: (o, s, t) for p, o, s, t in fo}
    order = [p for p, *_ in fs] + [p for p, *_ in fo if p not in ds]
    return [(p, ds.get(p), do.get(p)) for p in order if ds.get(p) != do.get(p)]


def pair(bs, bo, ls, lo):
    """pair stock defs with ours defs: exact signature first, then by member-name overlap."""
    ss = {t: sig(bs, t) for t in ls}
    so = {t: sig(bo, t) for t in lo}
    used, pairs = set(), []
    rest = []
    for t in ls:
        m = next((u for u in lo if u not in used and so[u] == ss[t]), None)
        if m is None:
            rest.append(t)
        else:
            used.add(m)
    for t in rest:
        names = {p for p, *_ in ss[t][1]}
        cand = [u for u in lo if u not in used]
        if not cand:
            if ss[t] not in set(so.values()):
                pairs.append((t, None))
            continue
        u = max(cand, key=lambda u: len(names & {p for p, *_ in so[u][1]}))
        used.add(u)
        pairs.append((t, u))
    sset = set(ss.values())
    for u in lo:
        if u not in used and so[u] not in sset:  # extra copy identical to a stock def = BTF dedup duplicate
            pairs.append((None, u))
    return pairs, ss, so


def main(fs, fo, out):
    bs, bo = BTF.load(fs), BTF.load(fo)
    S, O = bs.structs(), bo.structs()
    res = {}
    for name in sorted(set(S) | set(O)):
        ls, lo = S.get(name, []), O.get(name, [])
        if not ls or not lo:
            res[name] = {'status': 'only_stock' if ls else 'only_ours', 'size': [bs.types[t][2] for t in ls] or [bo.types[t][2] for t in lo]}
            continue
        pairs, ss, so = pair(bs, bo, ls, lo)
        diffs = []
        for t, u in pairs:
            if t is None or u is None:
                diffs.append({'stock_id': t, 'ours_id': u, 'unpaired': True,
                              'size': [ss[t][0] if t else None, so[u][0] if u else None]})
                continue
            if ss[t] == so[u]:
                continue
            md = mdiff(ss[t][1], so[u][1])
            diffs.append({'stock_id': t, 'ours_id': u, 'size': [ss[t][0], so[u][0]], 'members': md,
                          'first': md[0][0] if md else None})
        if diffs:
            res[name] = {'status': 'differs', 'ndefs': [len(ls), len(lo)], 'diffs': diffs}
    differing_s = {t for n, r in res.items() if r['status'] == 'differs' for d in r['diffs'] if d.get('stock_id') for t in [d['stock_id']]}
    # deep: identical own layout but embeds (by value) a differing struct
    for name, ls in S.items():
        if name in res or name not in O:
            continue
        for t in ls:
            emb = bs.embedded(t) & differing_s
            if emb:
                res[name] = {'status': 'deep', 'via': sorted({bs.tstr(e) for e in emb})}
                break
    json.dump(res, open(out + '.json', 'w'), indent=1)
    with open(out + '.txt', 'w') as f:
        cnt = {}
        for r in res.values():
            cnt[r['status']] = cnt.get(r['status'], 0) + 1
        f.write('counts: %s\n' % cnt)
        for name, r in res.items():
            if r['status'] != 'differs':
                continue
            for d in r['diffs']:
                f.write('\n== %s  size stock=%s ours=%s  ndefs=%s%s\n' % (name, d['size'][0], d['size'][1], r['ndefs'],
                                                                      '  UNPAIRED' if d.get('unpaired') else ''))
                for p, a, b in d.get('members', [])[:40]:
                    fa = '0x%x/%d %s' % (a[0] // 8, a[1], a[2]) if a else '-'
                    fb = '0x%x/%d %s' % (b[0] // 8, b[1], b[2]) if b else '-'
                    f.write('   %-36s stock %-50s ours %s\n' % (p, fa, fb))
                if len(d.get('members', [])) > 40:
                    f.write('   ... %d more\n' % (len(d['members']) - 40))
        f.write('\n-- deep (embed a differing struct by value):\n')
        for name, r in res.items():
            if r['status'] == 'deep':
                f.write('  %s via %s\n' % (name, ', '.join(r['via'])))
        f.write('\n-- only_ours:\n  ' + ' '.join(n for n, r in res.items() if r['status'] == 'only_ours') + '\n')
        f.write('\n-- only_stock:\n  ' + ' '.join(n for n, r in res.items() if r['status'] == 'only_stock') + '\n')
    print(open(out + '.txt').read()[:400])


if __name__ == '__main__':
    main(*sys.argv[1:4])
