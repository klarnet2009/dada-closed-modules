#!/usr/bin/env python3
"""fnd.py <module.ko> func... : disassembly with resolved relocation names (compare_ko_v2 naming), instruction offsets (hex), branch targets as offsets.
Optional env FROM/TO (hex offsets) limit the printed range."""
import os, re, subprocess, sys
sys.path.insert(0, '<work-dir>')
import compare_ko_v2 as C
B = '<build-dir>'
path = sys.argv[1]
fr = C.Elf(path).functions()
txt = subprocess.check_output([B, '-d', '--no-show-raw-insn', path], text=True)
want = sys.argv[2:]
lo = int(os.environ.get('FROM', '0'), 16)
hi = int(os.environ.get('TO', 'ffffff'), 16)
cur, base = None, 0
for l in txt.split('\n'):
    m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
    if m:
        cur = re.sub(r'\.llvm\.\d+$', '', m.group(1))
        base = int(l.split()[0], 16)
        if cur in want:
            print('=== ' + cur)
        continue
    if cur not in want:
        continue
    m = re.match(r'^\s+([0-9a-f]+):\s+(.*)$', l)
    if not m:
        continue
    off = int(m.group(1), 16) - base
    if not lo <= off <= hi:
        continue
    ins = m.group(2)
    ins = re.sub(r'\s*//.*$', '', ins)
    ins = re.sub(r'0x([0-9a-f]+) <[^>]*>', lambda mm: 'L%x' % (int(mm.group(1), 16) - base), ins)
    ins = re.sub(r'\s+', ' ', ins)
    rl = {o: t for o, _ty, t in fr[cur][1]}
    if off in rl:
        ins += '   ; ' + re.sub(r'end:\w+', '', rl[off]).replace('\n', '\\n')
    print('%4x: %s' % (off, ins))
