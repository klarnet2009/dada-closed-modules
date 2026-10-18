#!/usr/bin/env python3
"""calls.py stock.ko built.o stock.dis built.dis: per function, multiset of direct call targets, indirect call kCFI typeids,
atomic RMW / exclusive ops with their address offset, barriers and acquire loads; prints functions where they differ.
Inlining differences (stock calls X, ours inlines X or vice versa) show up here and are listed for manual classification."""
import collections, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('rvm', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rv_lib.py'))
rv = importlib.util.module_from_spec(spec); spec.loader.exec_module(rv)


def feats(ko, dis):
    e = rv.Elf(ko)
    fs = e.functions()
    da = rv.parse_dis(dis)
    out = {}
    for f, (code, rel, pre, bind) in fs.items():
        if f not in da:
            continue
        c = collections.Counter()
        lst = rv.emit(da[f][1], da[f][0], len(code), rel)
        w17 = {}
        for off, mn, ops in lst:
            if mn == 'bl':
                m = re.search(r'\{([^}]*)\}', ops)
                c['call ' + (m.group(1) if m else '?')] += 1
            elif mn == 'movk' and ops.startswith('w17'):
                mm = re.search(r'#(0x[0-9a-f]+)(, lsl #16)?', ops)
                w17['hi' if mm.group(2) else 'lo'] = mm.group(1)
            elif mn == 'blr':
                c['icall typeid %s%s' % (w17.get('hi', '?'), w17.get('lo', '?')[2:])] += 1
                w17 = {}
            elif rv.ATOM.match(mn) or rv.CAS.match(mn) or rv.STATOM.match(mn) or mn.startswith(('ldxr', 'ldaxr', 'stxr', 'stlxr')):
                m = re.search(r'\[\w+(, #(-?0x[0-9a-f]+))?\]', ops)
                c['atomic %s' % mn] += 1
            elif mn in ('dmb', 'dsb'):
                c['barrier %s %s' % (mn, ops)] += 1
            elif mn.startswith(('ldar', 'ldapr')):
                c['acquire-load'] += 1
            elif mn.startswith('stlr'):
                c['release-store'] += 1
            elif mn == 'brk':
                c['brk ' + ops] += 1
        out[f] = c
    return out


A = feats(sys.argv[1], sys.argv[3])
B = feats(sys.argv[2], sys.argv[4])
for f in sorted(A):
    if f not in B:
        continue
    a, b = A[f], B[f]
    d = ['%+d %s' % (b[k] - a[k], k) for k in sorted(set(a) | set(b)) if a[k] != b[k]]
    if d:
        print(f + ': ' + '; '.join(d))
