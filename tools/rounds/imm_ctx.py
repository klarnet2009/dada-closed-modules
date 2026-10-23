"""imm_ctx.py <stock.ko> <built.ko> <imm_report.txt> : for the functions listed in an imm_diff.py report that have SUSPECT pairs, find in both
modules the `mov Rx, #imm` of every stock-only immediate (resp. built-only) and print the next call target within 8 instructions.
Histogram of (callee) shows whether the differing immediates are line-number arguments (qdf_mem_malloc, qdf_trace*, self_recovery, ...)."""
import collections, re, subprocess, sys
B = '<build-dir>'
stock, built, rep = sys.argv[1:4]
funcs = [l.split()[0] for l in open(rep) if 'SUSPECT' in l and 'SUSPECT []' not in l and 'NOIMMDIFF' not in l]
want = set(funcs)


def parse(p):
    txt = subprocess.check_output([B, '-dr', '--no-show-raw-insn', p], text=True)
    res = {}
    cur = None
    for l in txt.split('\n'):
        l = re.sub(r'\s*//.*$', '', l)
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            cur = re.sub(r'\.llvm\.\d+$', '', m.group(1))
            if cur in want:
                res[cur] = []
            continue
        if cur not in res:
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+R_AARCH64_(CALL26|JUMP26)\s+(\S+)', l)
        if m:
            res[cur].append(('call', re.sub(r'\.llvm\.\d+$', '', m.group(2))))
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+(mov|movk)\s+[xw]\d+, #(-?0x[0-9a-f]+)(?:, lsl #(\d+))?\s*$', l)
        if m:
            res[cur].append(('imm', int(m.group(2), 16) << int(m.group(3) or 0)))
    return res


def ctx(lst):
    out = collections.defaultdict(list)
    for i, (k, v) in enumerate(lst):
        if k == 'imm':
            for j in range(i + 1, min(i + 14, len(lst))):
                if lst[j][0] == 'call':
                    out[v].append(lst[j][1])
                    break
    return out


a, b = parse(stock), parse(built)
hist = collections.Counter()
odd = []
for f in funcs:
    if f not in a or f not in b:
        continue
    ia = collections.Counter(v for k, v in a[f] if k == 'imm')
    ib = collections.Counter(v for k, v in b[f] if k == 'imm')
    ca, cb = ctx(a[f]), ctx(b[f])
    for v in (ia - ib):
        callee = ca[v][0] if ca[v] else '?'
        hist[callee] += 1
        if v < 0x100 or callee == '?':
            odd.append((f, hex(v), callee, 'stock'))
    for v in (ib - ia):
        callee = cb[v][0] if cb[v] else '?'
        hist[callee] += 1
        if v < 0x100 or callee == '?':
            odd.append((f, hex(v), callee, 'ours'))
for k, v in hist.most_common(25):
    print(v, k)
print('ODD', len(odd))
for o in odd[:40]:
    print(o)
