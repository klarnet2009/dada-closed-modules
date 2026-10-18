#!/usr/bin/env python3
"""kcfi_xm.py vmlinux stock.ko built.o stock.dis built.dis: for every indirect call through an xm_* kallsyms pointer, the kCFI
typeid expected at the call site (movk w17 pair after `ldur w16, [xN, #-4]`) in stock and built, vs the typeid word 4 bytes
before the target function in vmlinux (our running kernel). Mismatch = kCFI trap (panic) on that call."""
import collections, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('rvm', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rv_lib.py'))
rv = importlib.util.module_from_spec(spec); spec.loader.exec_module(rv)

XM = {}  # xm pointer name -> kernel symbol
src = open(sys.argv[6]).read() if len(sys.argv) > 6 else ''
for m in re.finditer(r'\{\s*"(\w+)",\s*0,\s*\(void \*\*\)&(\w+)\s*\}', src):
    XM[m.group(2)] = m.group(1)


def vmlinux_hash(path, names):
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<Q', d, 0x28)
    es, n, si = struct.unpack_from('<HHH', d, 0x3A)
    secs = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * es) for i in range(n)]
    symtab = next(s for s in secs if s[1] == 2)
    strtab = secs[symtab[6]]
    want = set(names)
    out = {}
    for off in range(symtab[4], symtab[4] + symtab[5], 24):
        nm, info, oth, sh, val, size = struct.unpack_from('<IBBHQQ', d, off)
        name = d[strtab[4] + nm:d.index(b'\0', strtab[4] + nm)].decode('latin-1')
        if name in want and (info & 15) == 2 and 0 < sh < n:
            s = secs[sh]
            fo = s[4] + (val - s[3])
            out.setdefault(name, []).append(struct.unpack_from('<I', d, fo - 4)[0])
    return out


def callsites(ko, dis):
    e = rv.Elf(ko)
    fs = e.functions()
    da = rv.parse_dis(dis)
    res = collections.defaultdict(set)
    for f, (code, rel, pre, bind) in fs.items():
        if f not in da:
            continue
        lst = rv.emit(da[f][1], da[f][0], len(code), rel)
        regsym = {}
        pend = None
        hi = lo = None
        for off, mn, ops in lst:
            m = re.search(r'\{obj:(xm_\w+)\}', ops)
            r0 = ops.split(',')[0].strip()
            if mn == 'ldr' and m:
                regsym['x' + r0[1:]] = m.group(1)
                continue
            if mn == 'ldur' and r0 == 'w16':
                base = re.search(r'\[(x\d+)', ops).group(1)
                pend = regsym.get(base)
                lo = hi = None
                continue
            if mn in ('movk', 'mov') and r0 == 'w17' and pend:
                mm = re.search(r'#(0x[0-9a-f]+)(, lsl #16)?', ops)
                if mm.group(2):
                    hi = int(mm.group(1), 16)
                else:
                    lo = int(mm.group(1), 16)
                if hi is not None and lo is not None:
                    res[pend].add(((hi << 16) | lo, f))
                    pend = None
                continue
            if mn in ('mov', 'add', 'ldr', 'adrp') and r0.startswith('x') and 'x' + r0[1:] in regsym and not m:
                regsym.pop('x' + r0[1:], None)
    return res


vm, sko, bko, sdis, bdis = sys.argv[1:6]
S, B = callsites(sko, sdis), callsites(bko, bdis)
names = sorted(set(S) | set(B))
H = vmlinux_hash(vm, [XM.get(x, x[3:]) for x in names])
bad = 0
for x in names:
    k = XM.get(x, x[3:])
    kh = H.get(k)
    st = sorted(set(t for t, f in S.get(x, ())))
    bt = sorted(set(t for t, f in B.get(x, ())))
    ok = bt == st and kh is not None and all(t in kh for t in bt)
    bad += not ok
    print('%-4s %-32s kernel:%-28s stock:%s built:%s %s' % ('OK' if ok else 'BAD', x, k + '=' + ','.join('%#x' % h for h in (kh or [])),
          ','.join('%#x' % t for t in st), ','.join('%#x' % t for t in bt), sorted(set(f for t, f in B.get(x, ())))[:3]))
print('xm pointers with indirect calls: %d, problems: %d; table entries never called indirectly: %s' % (len(names), bad, sorted(set(XM) - set(names))))
