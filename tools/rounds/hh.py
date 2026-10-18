"""hh.py <func> <ko> [<ko2>]: set of kCFI hashes (movk w17 pairs) tested inside <func> of each ko and the diff"""
import subprocess, sys, re
O = '<build-dir>'
fn = sys.argv[1]
def hashes(ko):
    t = subprocess.check_output([O, '-d', '--no-show-raw-insn', '--disassemble-symbols=' + fn, ko], text=True)
    ins = [l.split('\t', 1)[1].strip() if '\t' in l else '' for l in t.split('\n')]
    out = []; lo = None
    n = 0
    for l in ins:
        n += 1 if l else 0
        m = re.match(r'movk\s+w17, #(0x[0-9a-f]+)$', l)
        if m: lo = int(m.group(1), 16)
        m = re.match(r'movk\s+w17, #(0x[0-9a-f]+), lsl #16', l)
        if m and lo is not None: out.append(lo | (int(m.group(1), 16) << 16)); lo = None
    return n, out
res = [hashes(k) for k in sys.argv[2:]]
for k, (n, h) in zip(sys.argv[2:], res):
    print(k.split('/')[-1], 'insns', n, 'hash checks', len(h), 'unique', len(set(h)))
if len(res) == 2:
    a, b = set(res[0][1]), set(res[1][1])
    print('only first', [hex(x) for x in sorted(a - b)]); print('only second', [hex(x) for x in sorted(b - a)])
