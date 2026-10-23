#!/usr/bin/env python3
"""G40a: per-module split-BTF scan against the STOCK vmlinux BTF.

modbtf.py <stock.btf> <diff1.json> <out.json> <ko>...
For every .ko with a .BTF section:
 - refs   : named struct/union of the base that the module's own types point at (through ptr/typedef/cv/array)
 - redefs : struct/union defined locally under a name that also exists in the base
            ('same' = identical layout to a base copy, i.e. a dedup leftover; 'DIFF' = module saw another layout)
 - bad    : local types that reference a base id of an impossible kind (sanity check that the base is right)
 - hits   : refs/redefs that are in the differing set of layoutdiff (diff1.json)
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from btfparse import BTF, elf_section

REFKINDS = ('ptr', 'typedef', 'const', 'volatile', 'restrict', 'type_tag', 'var', 'func', 'decl_tag')


def refs_of(t):
    k, n, st, kf, vl, ex = t
    if k in ('struct', 'union'):
        return [m[1] for m in ex]
    if k == 'func_proto':
        return [st] + [a[1] for a in ex]
    if k == 'array':
        return [ex[0], ex[1]]
    if k in REFKINDS:
        return [st]
    if k == 'datasec':
        return [v[0] for v in ex]
    return []


def resolve(b, t):
    seen = 0
    while seen < 32:
        k = b.types[t][0]
        if k in ('struct', 'union', 'fwd'):
            return t
        if k == 'array':
            t = b.types[t][5][0]
        elif k in ('ptr', 'typedef', 'const', 'volatile', 'restrict', 'type_tag'):
            t = b.types[t][2]
        else:
            return None
        seen += 1
    return None


def scan(base, sec, ko, bstructs, bsig, differing):
    b = BTF(sec, base)
    refs, bad = set(), 0
    for i in range(b.nbase, len(b.types)):
        for r in refs_of(b.types[i]):
            if r >= len(b.types):
                bad += 1
                continue
            if r and r < b.nbase:
                if base.types[r][0] in ('func', 'datasec', 'decl_tag'):
                    bad += 1
                s = resolve(b, r)
                if s and b.types[s][1]:
                    kk = 'union' if b.types[s][0] == 'union' else 'struct'
                    refs.add(kk + ' ' + b.types[s][1])
    redefs = {}
    for n, ids in b.structs(local_only=True).items():
        if n in bstructs:
            same = all((b.types[t][2], tuple(b.flat(t))) in bsig[n] for t in ids)
            redefs[n] = 'same' if same else 'DIFF'
    return {'path': ko, 'btf': True, 'nlocal': len(b.types) - b.nbase, 'bad': bad,
                 'refs': sorted(refs), 'redefs': redefs,
                 'hits': sorted((refs | set(redefs)) & differing)}


def main():
    base = BTF.load(sys.argv[1])
    diff = json.load(open(sys.argv[2]))
    differing = {n for n, r in diff.items() if r['status'] in ('differs', 'deep')}
    bstructs = base.structs()
    bsig = {n: {(base.types[t][2], tuple(base.flat(t))) for t in ids} for n, ids in bstructs.items()}
    out = {}
    for ko in sys.argv[4:]:
        data = open(ko, 'rb').read()
        sec = elf_section(data, '.BTF')
        name = os.path.basename(ko)
        if sec is None:
            out.setdefault(name, {'path': ko, 'btf': False})
            continue
        try:
            out[name] = scan(base, sec, ko, bstructs, bsig, differing)
        except (ValueError, IndexError, KeyError, UnicodeDecodeError) as e:  # ids/strings outside the stock base: built on another base
            out[name] = {'path': ko, 'btf': True, 'parse_error': repr(e)[:80]}
    json.dump(out, open(sys.argv[3], 'w'), indent=1)
    nb = sum(1 for v in out.values() if v['btf'])
    print('modules %d, with BTF %d, bad-ref modules %d, parse errors %d' % (
        len(out), nb, sum(1 for v in out.values() if v.get('bad')), sum(1 for v in out.values() if 'parse_error' in v)))
    print('parse errors:', ' '.join(sorted(k for k, v in out.items() if 'parse_error' in v)))
    print('bad refs:', ' '.join('%s(%d)' % (k, v['bad']) for k, v in sorted(out.items()) if v.get('bad')))
    cnt = {}
    for v in out.values():
        for h in v.get('hits', []):
            cnt.setdefault(h, []).append(os.path.basename(v['path']))
    for h, l in sorted(cnt.items(), key=lambda x: -len(x[1])):
        print('%-40s %3d  %s' % (h, len(l), ' '.join(sorted(l)[:12])))
    rd = {}
    for k, v in out.items():
        for n, s in v.get('redefs', {}).items():
            if s == 'DIFF':
                rd.setdefault(n, []).append(k)
    print('\nlocal redefinitions with a layout different from the stock base:')
    for n, l in sorted(rd.items(), key=lambda x: -len(x[1])):
        print('%-40s %3d  %s' % (n, len(l), ' '.join(sorted(l)[:10])))


main()
