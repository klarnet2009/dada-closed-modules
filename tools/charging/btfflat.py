#!/usr/bin/env python3
"""btfflat.py <module.ko> <struct> : flattened member offsets of a struct from the module's split BTF (base unknown, local mode of btf_dump2.py).
Output: '<hexoff> <path> <size-ish type name>' one per scalar/pointer leaf. Arrays of structs are expanded for the first 4 elements."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, '<work-dir>')
from btf_dump2 import Btf, mod_btf
b = Btf(mod_btf(sys.argv[1]), None, local=True)
import collections
_f = [t['st'] for t in b.types if t['kind'] == 'func']; _p = [j for j, t in enumerate(b.types) if t['kind'] == 'func_proto']
_c = collections.Counter(st - j for st in _f for j in _p)
b.start_id = int(os.environ.get('BTF_START', _c.most_common(1)[0][0]))  # most frequent func->proto offset
_g = b.get
def sg(i):
    try:
        t = _g(i)
        if i >= b.start_id and (i - b.start_id) < 0: raise IndexError
        return t
    except IndexError:
        return dict(kind='int', name='?%d' % i, vlen=0, members=[], st=4)
b.get = sg
def sz(t, depth=0):
    k = t['kind']
    if k in ('struct','union'): return t['st']
    if k == 'array':
        et = b.get(t['arr'][0]); return sz(et)*t['arr'][2] if True else 0
    if k in ('typedef','const','volatile','restrict'): return sz(b.get(t['st']))
    if k == 'ptr': return 8
    if k == 'int': return t['st'] if 'st' in t else 4
    if k == 'enum': return 4
    return t.get('st', 0) if isinstance(t.get('st', 0), int) else 0
def tn(t):
    k = t['kind']
    if k in ('typedef','const','volatile'): return tn(b.get(t['st']))
    return k + ':' + (t['name'] or '')
def walk(t, base, path, out, depth):
    k = t['kind']
    if k in ('typedef','const','volatile','restrict'):
        return walk(b.get(t['st']), base, path, out, depth)
    if k in ('struct','union') and depth < 6:
        for (no, ty, off) in t['members']:
            nm = b.s(no)
            walk(b.get(ty), base + off//8, path + ('.' if path else '') + nm, out, depth+1)
        return
    if k == 'array':
        et = b.get(t['arr'][0]); n = t['arr'][2]
        while et['kind'] in ('typedef','const','volatile'): et = b.get(et['st'])
        if et['kind'] in ('struct','union'):
            esz = et['st']
            for i in range(min(n, 4)): walk(et, base + i*esz, '%s[%d]' % (path, i), out, depth+1)
            if n > 4: out.append((base + 4*esz, path + '[4..%d]' % (n-1), 'more'))
        else:
            esz2 = max(sz(et), 1) if et['kind'] != 'ptr' else 8
            if n <= 16:
                for i in range(n): out.append((base + i*esz2, '%s[%d]' % (path, i), tn(et)))
            else: out.append((base, '%s[0..%d]' % (path, n-1), tn(et)))
        return
    out.append((base, path, tn(t)))
want = sys.argv[2]
for t in b.types:
    if t['kind'] == 'struct' and t['name'] == want:
        out = []; walk(t, 0, '', out, 0)
        import re as _re
        for o, p, ty in out:
            p = _re.sub(r'\?str\d+', 'f_%x' % o, p)
            print('%x %s %s' % (o, p, ty))
        print('# size', t['st']); break
else:
    print('struct not found')
