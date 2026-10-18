"""ldst.py <module> [built.o] [minfn]: per function, global scalar accesses (R_AARCH64_LDST*_ABS_LO12_NC relocs), stock vs built, targets resolved to
symbol+delta (stock section-relative targets are resolved through the stock local symbol table). Prints functions where the stock accesses more globals than ours,
with the stock-only and ours-only targets (names normalised: .bss.x -> x, .llvm.N dropped)."""
import bisect, collections, re, subprocess, sys
B = '<build-dir>'
mod = sys.argv[1]
built = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] != '-' else '<work-dir>' % mod
mind = int(sys.argv[3]) if len(sys.argv) > 3 else 1
stock = '<work-dir>' % mod


def norm(t):
    t = re.sub(r'\.llvm\.\d+', '', t)
    return re.sub(r'^\.(bss|data|rodata|data\.\.read_mostly|data\.\.ro_after_init|data\.\.percpu)\.(?=[A-Za-z_])', '', t)


def symtab(path):
    out = subprocess.check_output([B + 'llvm-nm', '--format=sysv', '-S', path], text=True)
    secs = collections.defaultdict(list)
    for l in out.split('\n'):
        p = [x.strip() for x in l.split('|')]
        if len(p) < 7 or not re.match(r'^[0-9a-f]+$', p[1]): continue
        if p[3] not in ('OBJECT', 'NOTYPE') and p[2] not in 'bBdD': continue
        sec = p[6]
        if sec.startswith('.') :
            secs[sec].append((int(p[1], 16), int(p[4] or '0', 16) if re.match(r'^[0-9a-f]+$', p[4] or '') else 0, norm(p[0])))
    for s in secs: secs[s].sort()
    return secs


def resolve(secs, t):
    m = re.match(r'^(\.[A-Za-z_.]+?)(?:\+0x([0-9a-f]+))?$', t)
    if m and m.group(1) in secs:
        off = int(m.group(2) or '0', 16); lst = secs[m.group(1)]
        i = bisect.bisect_right([x[0] for x in lst], off) - 1
        if i >= 0:
            return '%s+%#x' % (lst[i][2], off - lst[i][0]) if off != lst[i][0] else lst[i][2]
    return norm(t)


def parse(path, secs=None):
    txt = subprocess.check_output([B + 'llvm-objdump', '-dr', '--no-show-raw-insn', path], text=True)
    res, cur = {}, None
    for l in txt.split('\n'):
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            cur = re.sub(r'\.llvm\.\d+$', '', m.group(1)); res[cur] = []; continue
        if cur is None: continue
        m = re.match(r'^\s+[0-9a-f]+:\s+R_AARCH64_LDST(\d+)_ABS_LO12_NC\s+(\S+)', l)
        if m:
            t = m.group(2)
            res[cur].append('%s %s' % (m.group(1), resolve(secs, t) if secs else norm(t)))
    return res

a, b = parse(stock, symtab(stock)), parse(built, symtab(built) if built.endswith('.ko') else None)
n = 0
for f in sorted(a):
    if f not in b: continue
    ca, cb = len(a[f]), len(b[f])
    if abs(ca - cb) >= mind:
        n += 1
        so = collections.Counter(a[f]) - collections.Counter(b[f]); oo = collections.Counter(b[f]) - collections.Counter(a[f])
        print('%-36s stock %3d ours %3d | stock-only: %s | ours-only: %s' % (f, ca, cb,
              ', '.join('%s x%d' % (k, v) for k, v in sorted(so.items()))[:260], ', '.join('%s x%d' % (k, v) for k, v in sorted(oo.items()))[:160]))
print('%s: functions where stock has >= %d more global accesses: %d of %d' % (mod, mind, n, len(a)))
