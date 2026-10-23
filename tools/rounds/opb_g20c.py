"""opb.py [built.o] [func ...]: batch version of opm.py. For every function (or the named ones) prints the number of differing instructions of the
register-masked MULTISET diff stock vs built, after dropping the noise classes: `mov R, R`, `add R, SP, #x`, call targets that differ only in local/global
naming. env V=1 prints the differing lines, MIN=n hides functions with fewer than n differing instructions."""
import collections, os, re, subprocess, sys
B = '<build-dir>'
args = sys.argv[1:]
MOD = os.environ.get('MOD', 'matt')
built = args[0] if args and args[0].endswith(('.o', '.ko')) else '<work-dir>' % MOD
want = [x for x in args if not x.endswith(('.o', '.ko'))]
STOCK = '<work-dir>' % MOD
NOISE = re.compile(r'^(mov R, R|add R, SP, #0x[0-9a-f]+|mov R, SP)$')


def parse(path):
    txt = subprocess.check_output([B, '-dr', '--no-show-raw-insn', path], text=True)
    res, cur = {}, None
    for l in txt.split('\n'):
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            cur = re.sub(r'\.llvm\.\d+$', '', m.group(1))
            res[cur] = []
            continue
        if cur is None:
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+R_AARCH64_(\w+)\s+(\S+)', l)
        if m:
            if m.group(1) in ('CALL26', 'JUMP26'):
                t = re.sub(r'\.llvm\.\d+$', '', m.group(2))
                t = re.sub(r'^\.text\.', '', t)
                res[cur].append('CALL ' + (t if not t.startswith('.text+') else '?'))
            continue
        if re.match(r'^\s+[0-9a-f]+:\s', l):
            ins = l.split(':', 1)[1].strip()
            if '.word' in ins:
                continue
            ins = re.sub(r'<[^>]*>', '', ins)
            ins = re.sub(r'\s*//.*$', '', ins)
            if re.match(r'^(adrp|adr)\s', ins) or re.match(r'^add\s+\w+, \w+, #0x0$', ins) or re.match(r'^bl\s', ins):
                continue
            ins = re.sub(r'^(b\.?\w*|cbn?z|tbn?z)(\s.*?)0x[0-9a-f]+\s*$', r'\1\2L', ins)
            ins = re.sub(r'\b[xw](\d+|zr)\b', 'R', ins)
            ins = re.sub(r'\bsp\b', 'SP', ins)
            res[cur].append(re.sub(r'\s+', ' ', ins))
    return res


a, b = parse(STOCK), parse(built)
rows = []
for f in sorted(a):
    if want and f not in want:
        continue
    if f not in b:
        rows.append((10 ** 6, f, ['MISSING']))
        continue
    ca = collections.Counter(x for x in a[f] if not NOISE.match(x) and not x.startswith('CALL ?'))
    cb = collections.Counter(x for x in b[f] if not NOISE.match(x) and not x.startswith('CALL ?'))
    d = ['%+d %s' % (cb[k] - ca[k], k) for k in sorted(set(ca) | set(cb)) if ca[k] != cb[k]]
    rows.append((sum(abs(cb[k] - ca[k]) for k in set(ca) | set(cb)), f, d))
mn = int(os.environ.get('MIN') or 0)
rows.sort()
for n, f, d in rows:
    if n >= mn:
        print('%5d %s (stock %d built %d)' % (n, f, len(a[f]), len(b.get(f, []))))
        if os.environ.get('V'):
            for x in d:
                print('        ', x)
