"""mo.py <hexoff|decoff>... : map offsets of struct matt_sock (env ST=struct name) to a field path using matt/btf_mod.txt (the BTF dump)
e.g.  python3 mo.py 0x618 0xa54 0x1198"""
import re, sys, os
here = os.path.dirname(os.path.abspath(__file__))
txt = open(os.path.join(here, 'matt', os.environ.get('BTF','btf_mod.txt')), encoding='utf8', errors='replace').read()
structs = {}
for m in re.finditer(r'^(struct|union) (\w+) \{\n(.*?)^\}; /\* size (\d+) \*/', txt, re.M | re.S):
    mem = []
    for l in m.group(3).split('\n'):
        mm = re.match(r'\t(.*?)(\w+)((?:\[\d+\])*)(?: : \d+)?;\t/\* \+(\d+) \*/', l)
        if mm:
            mem.append((mm.group(1).strip(), mm.group(2), mm.group(3), int(mm.group(4))))
    structs[m.group(1) + ' ' + m.group(2)] = (int(m.group(4)), mem)


def sz(t):
    t = t.strip()
    if t in structs:
        return structs[t][0]
    if t.endswith('*'):
        return 8
    d = {'u8': 1, 'u16': 2, 'u32': 4, 'u64': 8, '__be32': 4, '__be16': 2, '__u8': 1, '__u32': 4, 'int': 4, 'unsigned int': 4, 'long': 8, 'unsigned long': 8, 'bool': 1, '__u16': 2, 'unsigned short': 2, 'unsigned char': 1, 's32': 4, 's64': 8, '__u64': 8, 'char': 1, 'short': 2, 'u16 ': 2, '__be64': 8, '__s32': 4}
    return d.get(t, 0)


def walk(t, off, pre=''):
    if t not in structs:
        return pre + '+%d' % off
    size, mem = structs[t]
    best = None
    for (ty, nm, arr, o) in mem:
        if o <= off:
            best = (ty, nm, arr, o)
    if not best:
        return pre + '?'
    ty, nm, arr, o = best
    rest = off - o
    if arr:
        n = [int(x) for x in re.findall(r'\d+', arr)]
        es = sz(ty)
        if es:
            idx = rest // es
            rest -= idx * es
            return walk(ty, rest, pre + nm + '[%d].' % idx) if ty in structs else pre + nm + '[%d]' % idx + ('+%d' % rest if rest else '')
    if ty in structs and rest < structs[ty][0]:
        return walk(ty, rest, pre + nm + '.')
    return pre + nm + ('+%d' % rest if rest else '')


st = os.environ.get('ST', 'struct matt_sock')
for a in sys.argv[1:]:
    v = int(a, 0)
    print('%s +%#x: %s' % (st, v, walk(st, v)))
