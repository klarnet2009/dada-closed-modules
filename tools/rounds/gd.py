"""gd.py CHIP[,CHIP..] [--built=FILE] [--func=aw882xx_init] [--stop=...] [--show]: compare the final aw_device store state (stock vs built) after cmm + init block.
Names come from <work-dir> (clang -fdump-record-layouts, struct aw_device). Prints per word: name, stock, built."""
import re, sys
sys.path.insert(0, '<work-dir>')
import gx

D = '<work-dir>'
opts = dict(x[2:].split('=', 1) if '=' in x else (x[2:], '1') for x in sys.argv[1:] if x.startswith('--'))
args = [x for x in sys.argv[1:] if not x.startswith('--')]
chips = [int(c, 0) for c in args[0].split(',')]
func = opts.get('func', 'aw882xx_init')
cmmf = opts.get('cmm', 'aw_dev_init_cmm')
stop = opts.get('stop', 'aw882xx_dev_check_ef_lock').split(',')
if 'rd' in opts:
    for kv in opts['rd'].split(','):
        k, v = kv.split(':')
        gx.READS[int(k, 0)] = int(v, 0)
gx.RODATA = open(D + '/dump/aw882xx_dlkm/rodata.bin', 'rb').read()

# layout names
lay = []
LSZ = {}
lines = open(D + '/o/layout.txt').read().split('\n')
start = [i for i, l in enumerate(lines) if re.match(r'^\s+0 \| struct aw_device$', l)][0]
cur = []
for l in lines[start + 1:]:
    m = re.match(r'^\s*(\d+) \|(\s+)(.*)$', l)
    if not m:
        break
    off, ind, rest = int(m.group(1)), len(m.group(2)), m.group(3)
    nm = rest.split()[-1]
    lvl = (ind - 1) // 2
    cur = cur[:lvl - 1] + [nm]
    ty = rest[:-len(nm)].strip()
    sz = 8 if ty.endswith('*') else 1 if ty in ('unsigned char', 'uint8_t', 'int8_t', 'bool', 'char') else 2 if ty in ('int16_t', 'uint16_t') else 4
    lay.append((off, '.'.join(cur)))
    LSZ['.'.join(cur)] = sz
lay.sort()


def name(off):
    best = None
    for o, n in lay:
        if o <= off:
            best = (o, n)
    if best and best[1].endswith('.item'):
        d = off - best[0]
        return '%s[%d].%s' % (best[1], d // 12, ['reg', 'mask', 'value'][d % 12 // 4])
    return '%s%s' % (best[1], '' if best[0] == off else '+%d' % (off - best[0])) if best else '?'


def runall(file, chip):
    funcs = gx.load(file)
    e0, s0 = gx.run(file, cmmf, 0, funcs=funcs)
    cmm = (cmmf, e0.mem, e0.ptr)
    e, st = gx.run(file, func, chip, cmm=cmm, stop=stop, funcs=funcs)
    return e, st, e0


FN = {}
for l in open(D + '/dump/aw882xx_dlkm/dis.txt', errors='replace'):
    m = re.match(r'^([0-9a-f]{16}) <([^>]+)>:\s*$', l)
    if m:
        FN[int(m.group(1), 16)] = m.group(2)


def sym_str(s):
    m = re.match(r'\.text\+0x([0-9a-f]+)$', s)
    if m and int(m.group(1), 16) in FN:
        return FN[int(m.group(1), 16)] + '@' + s
    m = re.match(r'\.rodata\+0x([0-9a-f]+)$', s)
    if m:
        o = int(m.group(1), 16)
        t = gx.RODATA[o:o + 60].split(b'\0')[0]
        return '"%s"@%s' % (t.decode('latin1'), s)
    return s


def gen(chip):
    file = D + '/dump/aw882xx_dlkm/dis.txt'
    es, ss, e0 = runall(file, chip)
    mem = dict(e0.mem)
    ptr = dict(e0.ptr)
    print('=== chip 0x%x stock block (vs cmm): status %s' % (chip, ss))
    first = True
    for ev in es.ev:
        if ev[0] == 'call':
            if ev[1] == 'aw_dev_init_cmm':
                continue
            print('  CALL %s %s' % (ev[1], [('0x%x' % x if isinstance(x, int) else x) for x in ev[2]]))
            continue
        _, o, sz, v = ev
        if isinstance(v, int):
            cur = [mem.get(o + i) for i in range(sz)]
            new = [(v >> (8 * i)) & 0xff for i in range(sz)]
            if cur == new:
                if 'same' in opts:
                    print('  (same) +0x%03x/%d %s=0x%x' % (o, sz, name(o), v))
                continue
            for i in range(sz):
                mem[o + i] = new[i]
            ptr.pop(o, None)
            parts = []
            k = 0
            while k < sz:
                n = name(o + k)
                step = 4 if sz - k >= 4 else sz - k
                if sz == 8 and k == 0 and n.split('.')[-1] in ('name', 'prof_name_list', 'prof_desc', 'private_data', 'dev', 'i2c'):
                    step = 8
                val = sum(new[k + i] << (8 * i) for i in range(step))
                parts.append('%s=0x%x' % (n, val))
                k += step
            print('  +0x%03x/%d %s' % (o, sz, ' '.join(parts)))
        elif isinstance(v, tuple) and v[0] == 'sym':
            print('  +0x%03x/%d %s = %s' % (o, sz, name(o), sym_str(v[1])))
        else:
            print('  +0x%03x/%d %s = UNKNOWN %s' % (o, sz, name(o), v))


def gen_c(chip, indent='	'):
    file = D + '/dump/aw882xx_dlkm/dis.txt'
    es, ss, e0 = runall(file, chip)
    mem = dict(e0.mem)
    mon = {}
    out = []
    mon_at = None
    def fld(path):
        return 'aw_dev->' + path
    for ev in es.ev:
        if ev[0] == 'call':
            if ev[1] != 'aw_dev_init_cmm':
                out.append('/* CALL %s %s */' % (ev[1], [('0x%x' % x if isinstance(x, int) else x) for x in ev[2]]))
            continue
        _, o, sz, v = ev
        if isinstance(v, tuple):
            if v[0] == 'sym':
                n = name(o)
                out.append('%s = %s;' % (fld(n), sym_str(v[1]).replace('@', ' /* ') + ' */'))
            else:
                out.append('/* UNKNOWN store +0x%x/%d */' % (o, sz))
            continue
        if 0x60 <= o < 0x92 and not (o >= 0x90):
            for i in range(sz):
                mon[o - 0x60 + i] = (v >> (8 * i)) & 0xff
            if mon_at is None:
                mon_at = len(out)
                out.append('@@MON@@')
            continue
        k = 0
        while k < sz:
            n = name(o + k)
            leaf = n.split('+')[0]
            lsz = LSZ.get(leaf, 4)
            lsz = min(lsz, sz - k)
            val = sum(((v >> (8 * (k + i))) & 0xff) << (8 * i) for i in range(lsz))
            out.append('%s = 0x%x;' % (fld(n), val))
            k += lsz
    if mon:
        ln = max(mon) + 1
        txt = bytes(mon[i] for i in range(ln)).decode('latin1')
        out[mon_at] = 'memcpy(aw_dev->monitor_name, "%s", %d);' % (txt, ln)
    return out


if 'genc' in opts:
    for chip in chips:
        print('/* ==== chip 0x%x ==== */' % chip)
        for x in gen_c(chip):
            print('	' + x)
    sys.exit(0)

if 'sum' in opts:
    for chip in chips:
        es, ss, e0 = runall(D + '/dump/aw882xx_dlkm/dis.txt', chip)
        nm = bytes(es.mem.get(0x60 + i, 0) for i in range(32)).split(bytes([0]))[0].decode('latin1')
        print('chip 0x%x: %s  name=%s  stores=%d calls=%s' % (chip, ss, nm, len([x for x in es.ev if x[0] == 'st']), [c[1] for c in es.ev if c[0] == 'call']))
    sys.exit(0)

if 'gen' in opts:
    for chip in chips:
        gen(chip)
    sys.exit(0)

for chip in chips:
    if 'cmmonly' in opts:
        es, ss = gx.run(D + '/dump/aw882xx_dlkm/dis.txt', cmmf, 0)
        eb, sb = gx.run(opts.get('built', D + '/o/aw882xx_dlkm.dis'), cmmf, 0)
    else:
        es, ss, _ = runall(D + '/dump/aw882xx_dlkm/dis.txt', chip)
        eb, sb, _ = runall(opts.get('built', D + '/o/aw882xx_dlkm.dis'), chip)
    ws, wb = gx.words(es.mem, es.ptr), gx.words(eb.mem, eb.ptr)
    print('=== chip 0x%x: stock %s (%d ev), built %s (%d ev)' % (chip, ss, len(es.ev), sb, len(eb.ev)))
    nd = 0
    for o in sorted(set(ws) | set(wb)):
        a, b = ws.get(o), wb.get(o)
        if a is not None and b is not None and a[0] == b[0] == 'p':
            same = sym_str(a[1]).split('@')[0] == sym_str(b[1]).split('@')[0] or (a[1].startswith('.rodata') and b[1].startswith('.'))
            if same:
                continue
        elif a == b:
            continue
        nd += 1
        f = lambda x: '-' if x is None else (('0x%x' % x[1]) if x[0] != 'p' else sym_str(x[1]))
        print('  +0x%03x %-34s stock %-24s built %s' % (o, name(o), f(a), f(b)))
    print('  -> %d word differences' % nd)
    cs = [(c[1], c[2][:3]) for c in es.ev if c[0] == 'call']
    cb = [(c[1], c[2][:3]) for c in eb.ev if c[0] == 'call']
    if 'show' in opts or [c[0] for c in cs] != [c[0] for c in cb]:
        print('  calls stock:', [c[0] for c in cs])
        print('  calls built:', [c[0] for c in cb])
