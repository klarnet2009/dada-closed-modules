"""tproto.py <stock.ko>: print the BTF prototype of every __traceiter_<evt> (param names/types as far as resolvable)"""
import os, sys
os.environ.setdefault('STR_BASE', '2552072'); os.environ.setdefault('START_ID', '141904')
sys.path.insert(0, '<work-dir>')
import btf
from pathlib import Path


def protos(ko):
    m = btf.Btf(btf.mod_btf(ko), None, local=True)
    r = btf.Render(m)
    out = {}
    for i, t in enumerate(m.types):
        if t['kind'] == 'func' and t['name'].startswith('__traceiter_'):
            fp = m.get(t['st'])
            ps = []
            for noff, pt in fp['members']:
                ps.append((m.s(noff), r.decl(pt, '')))
            out[t['name'][len('__traceiter_'):]] = ps
    return out


if __name__ == '__main__':
    for k, v in protos(sys.argv[1]).items():
        print(k, v)
