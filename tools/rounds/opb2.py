"""opb2.py <stock.ko> <built.ko> <funcs.txt|-> : register-masked multiset diff per function (adapted from g14j opb.py).
For each function prints  n_all n_noimm  name, where n_all = differing instructions of the multiset (reg-masked, noise dropped) and
n_noimm = the same after also masking the immediate of `mov/movk R, #imm` (__LINE__ style immediates, handled as CODEGEN-LINE).
env V=1 prints the differing lines of the noimm multiset, V=2 also the full one. MIN=n hides rows with n_noimm < n."""
import collections, os, re, subprocess, sys
B = '<build-dir>'
stock, built, fl = sys.argv[1:4]
want = None if fl == '-' else set(open(fl).read().split())
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
                res[cur].append('CALL')
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


def mk(ins):
    return re.sub(r'^(mov|movk)( R,) #0x[0-9a-f]+(, lsl #\d+)?$', r'\1\2 #IMM', ins)


a, b = parse(stock), parse(built)
rows = []
for f in sorted(a):
    if want is not None and f not in want:
        continue
    if f not in b:
        rows.append((10 ** 6, 10 ** 6, f, ['MISSING'], []))
        continue
    ca = collections.Counter(x for x in a[f] if not NOISE.match(x) and not x.startswith('CALL ?'))
    cb = collections.Counter(x for x in b[f] if not NOISE.match(x) and not x.startswith('CALL ?'))
    da = ['%+d %s' % (cb[k] - ca[k], k) for k in sorted(set(ca) | set(cb)) if ca[k] != cb[k]]
    na = sum(abs(cb[k] - ca[k]) for k in set(ca) | set(cb))
    ma = collections.Counter(mk(x) for x in ca.elements())
    mb = collections.Counter(mk(x) for x in cb.elements())
    dm = ['%+d %s' % (mb[k] - ma[k], k) for k in sorted(set(ma) | set(mb)) if ma[k] != mb[k]]
    nm = sum(abs(mb[k] - ma[k]) for k in set(ma) | set(mb))
    rows.append((nm, na, f, dm, da))
mn = int(os.environ.get('MIN') or 0)
rows.sort()
for nm, na, f, dm, da in rows:
    if nm >= mn:
        print('%5d %5d %s (stock %d built %d)' % (nm, na, f, len(a[f]), len(b.get(f, []))))
        if os.environ.get('V'):
            for x in dm:
                print('        ', x)
        if os.environ.get('V') == '2':
            for x in da:
                print('      F ', x)
