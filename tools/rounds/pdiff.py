"""pdiff.py <stock.ko> <built.ko> [func ...]: normalised per-function listing diff (difflib hunks only).
Normalisation: registers -> W/X, branch targets -> L, stack offsets masked, adrp page masked, brk imm masked, reloc symbols resolved
(string -> S"text", data -> symbol name, .text+off -> function name), `mov R, R`/`add R,sp,#x`/pac/bti dropped, .word padding dropped.
Hunks whose multiset of lines is equal on both sides and which contain no calls are dropped (pure scheduling). Printed -stock / +built.
env: Q=1 only names; MIN=n; MAXL=n lines per hunk side."""
import difflib, os, re, subprocess, sys
B = '<build-dir>'
R = B.replace('llvm-objdump', 'llvm-readelf')
NL = chr(10)


def secdata(path):
    out = subprocess.check_output([B, '-s', path], text=True, errors='replace')
    data, cur = {}, None
    for l in out.split(NL):
        m = re.match(r'^Contents of section (\S+):', l)
        if m:
            cur = m.group(1) if m.group(1).startswith('.rodata') else None
            if cur:
                data[cur] = bytearray()
            continue
        m = re.match(r'^ ([0-9a-f]+) ((?:[0-9a-f]{2,8} ?){1,4})', l)
        if m and cur:
            off = int(m.group(1), 16)
            bb = bytes.fromhex(m.group(2).replace(' ', ''))
            if len(data[cur]) < off:
                data[cur].extend(bytes(off - len(data[cur])))
            data[cur][off:off + len(bb)] = bb
    return data


def symtab(path):
    secs = {}
    for l in subprocess.check_output([R, '-S', '-W', path], text=True, errors='replace').split(NL):
        m = re.match(r'^\s*\[\s*(\d+)\]\s+(\S+)', l)
        if m:
            secs[int(m.group(1))] = m.group(2)
    st = {}
    BYNAME.clear()
    for l in subprocess.check_output([R, '-s', '-W', path], text=True, errors='replace').split(NL):
        m = re.match(r'^\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+(OBJECT|NOTYPE)\s+\w+\s+\w+\s+(\d+)\s+(\S+)', l)
        if m and int(m.group(4)) in secs:
            st.setdefault(secs[int(m.group(4))], []).append((int(m.group(1), 16), int(m.group(2)), m.group(5)))
            BYNAME[m.group(5)] = (secs[int(m.group(4))], int(m.group(1), 16))
            BYNAME.setdefault(re.sub(r'\.llvm\.\d+$', '', m.group(5)), BYNAME[m.group(5)])
    return st


BYNAME = {}


def resolve(sym, sd, st):
    isstr = sym.startswith(('.str', '__func__'))
    if sym in BYNAME and (isstr or BYNAME[sym][0] in sd):
        sym = '%s+0x%x' % BYNAME[sym]
    m = re.match(r'^(\.[\w.\-]+?)\+0x([0-9a-f]+)$', sym)
    if not m:
        m = re.match(r'^(\.[\w.\-]+)$', sym)
        if not m:
            return sym
        sec, off = m.group(1), 0
    else:
        sec, off = m.group(1), int(m.group(2), 16)
    for o, sz, nm in st.get(sec, []):
        if o == off and sz > 0 and not isstr and not sec.startswith('.rodata.str'):
            if not nm.startswith(('.str', '__func__')):
                return nm
    if sec in sd:
        d = sd[sec]
        if off < len(d):
            e = d.find(bytes(1), off)
            t = bytes(d[off:e])
            if len(t) >= 1 and all(32 <= c < 127 or c in (9, 10) or c >= 0xc2 for c in t[1:]) and (t[0] in (1, 2, 3, 4, 5, 6, 7, 9, 10) or 32 <= t[0] < 127):
                return 'S"%s"' % t.decode('latin1').replace(NL, '\\n')
    best = None
    for o, sz, nm in st.get(sec, []):
        if o <= off and (off < o + max(sz, 1) or o == off):
            if best is None or o > best[0]:
                best = (o, nm)
    if best:
        return best[1] + ('' if best[0] == off else '+%d' % (off - best[0]))
    for pre in ('.bss.', '.data.rel.ro.local.', '.data.rel.ro.', '.data.', '.rodata.', '.text.'):
        if sec.startswith(pre):
            return sec[len(pre):] + ('' if off == 0 else '+%d' % off)
    return sym


def parse(path):
    sd = secdata(path)
    st = symtab(path)
    txt = subprocess.check_output([B, '-dr', '--no-show-raw-insn', path], text=True, errors='replace')
    res, cur = {}, None
    sec, fmap = '', {}
    for l in txt.split(NL):
        m = re.match(r'^Disassembly of section (\S+):', l)
        if m:
            sec = m.group(1)
            continue
        m = re.match(r'^([0-9a-f]{16}) <([^>]+)>:$', l)
        if m:
            cur = re.sub(r'\.llvm\.\d+$', '', m.group(2))
            res[cur] = []
            fmap[(sec, int(m.group(1), 16))] = cur
            continue
        if cur is None:
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+R_AARCH64_(\w+)\s+(\S+)', l)
        if m:
            kind, sym = m.group(2), m.group(3)
            if not sym.startswith('.text'):
                sym = resolve(sym, sd, st)
            elif sym.startswith('.text.'):
                sym = re.sub(r'\+0x4$', '', sym[6:])
            sym = re.sub(r'\.llvm\.\d+', '', sym)
            if res[cur] and kind in ('ADD_ABS_LO12_NC', 'LDST8_ABS_LO12_NC', 'LDST16_ABS_LO12_NC', 'LDST32_ABS_LO12_NC', 'LDST64_ABS_LO12_NC', 'LDST128_ABS_LO12_NC', 'ADR_PREL_PG_HI21', 'CALL26', 'JUMP26', 'PREL32'):
                res[cur][-1] += ' @' + sym
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(.*)$', l)
        if not m:
            continue
        ins = m.group(2).split('//')[0].strip()
        ins = re.sub(r'\s+', ' ', ins)
        ins = re.sub(r'<[^>]*>', '', ins).strip()
        ins = re.sub(r'\b(b|bl|b\.\w+|cbz|cbnz|tbz|tbnz)( [wx]\d+,)?( #(?:0x)?[0-9a-f]+,)? 0x[0-9a-f]+', lambda k: k.group(1) + (k.group(2) or '') + (k.group(3) or '') + ' L', ins)
        ins = re.sub(r'\b(adrp?) (\w+), 0x[0-9a-f]+', r'\1 \2, P', ins)
        ins = re.sub(r'\bbrk #0x[0-9a-f]+', 'brk #K', ins)
        ins = re.sub(r'\b([wx])(\d+)\b', lambda k: 'W' if k.group(1) == 'w' else 'X', ins)
        ins = re.sub(r'\[sp(, #0x[0-9a-f]+)?\](!?)', r'[sp, #K]\2', ins)
        ins = re.sub(r'\badd X, sp, #0x[0-9a-f]+', 'add X, sp, #K', ins)
        ins = re.sub(r'\b[bsdqv](\d+)\b', 'F', ins)
        res[cur].append(ins)

    def fix(i):
        m = re.search(r' @(\.[\w.]+)\+0x([0-9a-f]+)$', i)
        if m and (m.group(1), int(m.group(2), 16)) in fmap:
            return i[:m.start()] + ' @' + fmap[(m.group(1), int(m.group(2), 16))]
        return i
    for k in res:
        res[k] = [fix(i) for i in res[k]]
    return res


def clean(lst):
    out = []
    for i in lst:
        if re.match(r'^(mov [WX], [WX]|mov X, sp|add X, sp, #K|nop|paciasp|autiasp|bti [cj]|hint .*)$', i):
            continue
        if '.word' in i:
            continue
        out.append(i)
    return out


POLS = [('eq', 'ne'), ('lt', 'ge'), ('le', 'gt'), ('hs', 'lo'), ('hi', 'ls'), ('mi', 'pl'), ('vs', 'vc'), ('cs', 'cc')]


def polnorm(t):
    t = re.sub(r'^b\.(\w+) ', lambda m: 'b.' + next(('%s|%s' % p for p in POLS if m.group(1) in p), m.group(1)) + ' ', t)
    t = re.sub(r'^(tbn?z|cbn?z) ', lambda m: m.group(1)[:-1] + 'z|nz ' if m.group(1).endswith('nz') else m.group(1) + '|nz ', t)
    return t


def residual(A, Bn, want):
    import collections
    mn = int(os.environ.get('MIN', '1'))
    for f in (want or sorted(A)):
        if f not in Bn or f not in A:
            continue
        x, y = clean(A[f]), clean(Bn[f])
        if x == y:
            continue
        if os.environ.get('DROP'):
            rd = re.compile(os.environ['DROP'])
            x = [t for t in x if not rd.search(t)]
            y = [t for t in y if not rd.search(t)]
        cx = collections.Counter(polnorm(t) for t in x)
        cy = collections.Counter(polnorm(t) for t in y)
        dx, dy = cx - cy, cy - cx
        n = sum(dx.values()) + sum(dy.values())
        if n < mn or n == 0:
            continue
        print('=== %s stock %d built %d  residual %d' % (f, len(x), len(y), n))
        if os.environ.get('Q'):
            continue
        for t in x:
            k = polnorm(t)
            if dx.get(k, 0) > 0:
                dx[k] -= 1
                print('  -', t)
        for t in y:
            k = polnorm(t)
            if dy.get(k, 0) > 0:
                dy[k] -= 1
                print('  +', t)


def main():
    a, b = sys.argv[1], sys.argv[2]
    want = sys.argv[3:]
    A, Bn = parse(a), parse(b)
    if os.environ.get('RES'):
        residual(A, Bn, want)
        return
    mn = int(os.environ.get('MIN', '1'))
    maxl = int(os.environ.get('MAXL', '30'))
    for f in (want or sorted(A)):
        if f not in Bn or f not in A:
            print('MISSING', f)
            continue
        x, y = clean(A[f]), clean(Bn[f])
        if os.environ.get('FILT'):
            rx = re.compile(os.environ['FILT'])
            x = [t for t in x if rx.search(t)]
            y = [t for t in y if rx.search(t)]
        if x == y:
            continue
        sm = difflib.SequenceMatcher(None, x, y, autojunk=False)
        hunks = []
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == 'equal':
                continue
            if sorted(x[i1:i2]) == sorted(y[j1:j2]) and not any(' @' in t or t.startswith(('bl', 'b ', 'blr', 'br ')) for t in x[i1:i2]):
                continue
            hunks.append((tag, i1, i2, j1, j2))
        dels = {}
        for h in hunks:
            if h[0] == 'delete':
                dels.setdefault(tuple(x[h[1]:h[2]]), []).append(h)
        for h in list(hunks):
            if h[0] == 'insert':
                k = tuple(y[h[3]:h[4]])
                if k in dels and dels[k] and len(k) >= 3:
                    hunks.remove(h)
                    hunks.remove(dels[k].pop())
        n = sum((i2 - i1) + (j2 - j1) for _, i1, i2, j1, j2 in hunks)
        if n < mn or not hunks:
            continue
        print('=== %s stock %d built %d  hunklines %d' % (f, len(x), len(y), n))
        if os.environ.get('Q'):
            continue
        for tag, i1, i2, j1, j2 in hunks:
            print('  @@ stock %d..%d / built %d..%d' % (i1, i2, j1, j2))
            ctx = int(os.environ.get('CTX', '0'))
            for s in x[max(0, i1 - ctx):i1]:
                print('   ', s)
            for s in x[i1:i2][:maxl]:
                print('  -', s)
            for s in y[j1:j2][:maxl]:
                print('  +', s)
            for s in x[i2:i2 + ctx]:
                print('   ', s)


main()
