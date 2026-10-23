#!/usr/bin/env python3
"""sdiff.py <A.ko> <B.ko> [func...] : per function normalised disassembly diff of two modules (CN stock vs global stock).
No func given: list functions whose normalised text differs with the number of +/- lines. With func names: unified diff.
Addresses are dropped, branch targets become L, relocation targets are kept by name (llvm suffix removed), adrp/add pairs keep the symbol."""
import difflib, os, re, subprocess, sys
B = '<build-dir>'


sys.path.insert(0, '<work-dir>')
import compare_ko_v2 as C


def parse(path):
    txt = subprocess.check_output([B, '-d', '--no-show-raw-insn', path], text=True)
    fr = C.Elf(path).functions()
    res, cur, base = {}, None, 0
    relmap = {}
    for k, v in fr.items():
        relmap[k] = {o: t for o, _ty, t in v[1]}
    for l in txt.split('\n'):
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            cur = re.sub(r'\.llvm\.\d+$', '', m.group(1))
            base = int(l.split()[0], 16)
            res[cur] = []
            continue
        if cur is None:
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s', l)
        if m:
            off = int(m.group(1), 16) - base
            ins = l.split(':', 1)[1].strip()
            ins = re.sub(r'<[^>]*>', '', ins)
            ins = re.sub(r'\s*//.*$', '', ins)
            ins = re.sub(r'^(b\.?\w*|cbn?z|tbn?z)(\s.*?)0x[0-9a-f]+\s*$', r'\1\2L', ins)
            ins = re.sub(r'\s+', ' ', ins)
            ins = re.sub(r'^(adrp \w+), 0x[0-9a-f]+', r'', ins)
            if os.environ.get('MASK'):
                ins = re.sub(r'[xw](\d+|zr)', 'R', ins)
            if re.match(r'^([0-9a-f]{2} ){4}\.word', ins):
                continue
            if off in relmap.get(cur, {}):
                ins += '  @' + re.sub(r'end:\w+', '', relmap[cur][off])
            res[cur].append(ins)
    return res


a, b = parse(sys.argv[1]), parse(sys.argv[2])
want = sys.argv[3:]
if not want:
    for f in sorted(a):
        if f in b and a[f] != b[f]:
            d = [x for x in difflib.unified_diff(a[f], b[f], lineterm='', n=0) if x[0] in '+-' and x[:3] not in ('+++', '---')]
            print('%-45s %5d -> %5d insns, %4d diff lines' % (f, len(a[f]), len(b[f]), len(d)))
    print('only A:', [f for f in a if f not in b])
    print('only B:', [f for f in b if f not in a])
else:
    for f in want:
        print('=== %s (A %d insns, B %d insns)' % (f, len(a.get(f, [])), len(b.get(f, []))))
        for x in difflib.unified_diff(a.get(f, []), b.get(f, []), lineterm='', n=int(__import__('os').environ.get('CTX', 3))):
            print(x)
