"""imm_diff.py <stock.ko> <built.ko> <funcs.txt>: per function, the stock-only / built-only multiset of mov/movk immediates (value << lsl),
paired by nearness; prints SUSPECT pairs that are not line-number-like (|delta| > 60 or stock value < 0x100)."""
import collections, re, subprocess, sys
B = '<build-dir>'
stock, built, fl = sys.argv[1:4]
want = set(open(fl).read().split())


def parse(p):
    txt = subprocess.check_output([B, '-d', '--no-show-raw-insn', p], text=True)
    res = {}
    cur = None
    for l in txt.split('\n'):
        l = re.sub(r'\s*//.*$', '', l)
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            cur = re.sub(r'\.llvm\.\d+$', '', m.group(1))
            res[cur] = collections.Counter()
            continue
        if cur is None or cur not in want:
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+(mov|movk)\s+[xw]\d+, #(-?0x[0-9a-f]+)(?:, lsl #(\d+))?\s*$', l)
        if m:
            res[cur][int(m.group(2), 16) << int(m.group(3) or 0)] += 1
    return res


a, b = parse(stock), parse(built)
for f in sorted(want):
    if f not in a or f not in b:
        continue
    so = sorted((a[f] - b[f]).elements())
    bo = sorted((b[f] - a[f]).elements())
    if not so and not bo:
        print(f, 'NOIMMDIFF')
        continue
    pairs = []
    bo2 = list(bo)
    for s in so:
        if bo2:
            j = min(range(len(bo2)), key=lambda i: abs(bo2[i] - s))
            pairs.append((s, bo2.pop(j)))
        else:
            pairs.append((s, None))
    d = [(o - s) if o is not None else None for s, o in pairs]
    big = [(hex(s), hex(o) if o is not None else None) for (s, o), x in zip(pairs, d) if x is None or abs(x) > 60 or s < 0x100]
    print(f, 'n=%d/%d' % (len(so), len(bo)), 'maxd=', max([abs(x) for x in d if x is not None] or [0]), 'SUSPECT', big[:8], 'extra_ours', [hex(x) for x in bo2][:6])
