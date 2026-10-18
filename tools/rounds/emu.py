"""emu.py <module> <func> [maxins]: tiny straight-line emulator for string-copy prologues (usage_show style).
Tracks adrp/add rodata pointers, ldr/ldp/ldur loads from .rodata, stores into the buffer passed in x2 (or x1) and prints the
reconstructed buffer bytes. Usage: python3 emu.py mi_reclaim usage_show (reads dis.txt/rodata.txt in g11_tools/<module>/)."""
import re, sys, os
mod, func = sys.argv[1], sys.argv[2]
base = os.path.join(os.path.dirname(os.path.abspath(__file__)), mod)
# rodata image
ro = bytearray(0x4000)
for line in open(os.path.join(base, 'rodata.txt'), encoding='utf8', errors='replace'):
    m = re.match(r"^\.rodata\+0x([0-9a-f]+)\t(.*)$", line.rstrip('\n'))
    if m:
        try:
            s = eval(m.group(2))
        except Exception:
            continue
        b = s.encode('latin-1') + b'\0'
        o = int(m.group(1), 16)
        ro[o:o + len(b)] = b
# function disassembly
txt = open(os.path.join(base, 'dis.txt'), errors='replace').read().split('\n')
out = []
inf = False
for l in txt:
    m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
    if m:
        if inf:
            break
        inf = (m.group(1) == func)
        continue
    if inf:
        out.append(l)
ins = []
for l in out:
    m = re.match(r'^		[0-9a-f]+:\s+R_AARCH64_(\S+)	(\S+)$', l)
    if m and ins:
        ins[-1][3] = m.group(2)
        continue
    m = re.match(r'^\s+([0-9a-f]+):\s+(\S+)	(.*)$', l)
    if m:
        ins.append([m.group(1), m.group(2), m.group(3).split('//')[0].split('<')[0].strip(), None])
        continue
    m = re.match(r'^\s+([0-9a-f]+):\s+(\S+)$', l)
    if m:
        ins.append([m.group(1), m.group(2), '', None])
        continue
LSTR = {'.L.str.136': 0x130a, '.L.str.141': 0x1338, '.L.str.142': 0x17a7}


def relro(rel):
    m = re.match(r'(\.rodata|\.L\.str\.\d+)\+0x([0-9a-f]+)', rel or '')
    if not m:
        return None
    return ('ro', (LSTR.get(m.group(1), 0)) + int(m.group(2), 16))


regs = {'x2': ('buf', 0), 'x0': ('buf', 0), 'x1': ('buf', 0)}
mem = {}
maxn = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9


def R(n):
    n = n.strip()
    if n in ('xzr', 'wzr'):
        return 0
    return regs.get('x' + n[1:])


def setR(n, v):
    if n in ('xzr', 'wzr'):
        return
    regs['x' + n[1:]] = v


def width(n):
    return 8 if n[0] == 'x' else 4


def addr(op):
    m = re.match(r'\[(\w+)(?:, #(-?0x[0-9a-f]+|-?\d+))?\](!)?$', op.strip())
    b = R(m.group(1))
    off = int(m.group(2), 0) if m.group(2) else 0
    return b, off, bool(m.group(3))


def load(b, off, w):
    if b is None or not isinstance(b, tuple):
        return None
    if b[0] == 'ro':
        return int.from_bytes(ro[b[1] + off:b[1] + off + w], 'little')
    return None


def store(b, off, val, w):
    if not isinstance(b, tuple) or b[0] != 'buf':
        return
    if isinstance(val, tuple) or val is None:
        val = 0
    for i, c in enumerate(val.to_bytes(8, 'little')[:w]):
        mem[b[1] + off + i] = c


cnt = 0
for pc, op, args, rel in ins:
    cnt += 1
    if cnt > maxn:
        break
    if op in ('bl', 'b', 'ret', 'paciasp', 'autiasp') or op.startswith('b.') or op.startswith('cb'):
        if op == 'bl':
            break
        continue
    a = [x.strip() for x in re.split(r',\s*(?![^\[]*\])', args)]
    if op == 'adrp':
        setR(a[0], relro(rel))
    elif op == 'add':
        v = R(a[1]) if a[1][0] in 'xw' else None
        imm = int(a[2].lstrip('#'), 0) if a[2].startswith('#') else None
        if rel and relro(rel):
            setR(a[0], relro(rel))
        elif isinstance(v, tuple) and imm is not None:
            setR(a[0], (v[0], v[1] + imm))
        else:
            setR(a[0], None)
    elif op == 'mov':
        if a[1].startswith('#'):
            setR(a[0], int(a[1].lstrip('#'), 0) & (2**64 - 1))
        else:
            setR(a[0], R(a[1]))
    elif op == 'movk':
        sh = int(a[2].split('#')[1], 0) if len(a) > 2 else 0
        v = R(a[0]) or 0
        v = (v & ~(0xffff << sh)) | (int(a[1].lstrip('#'), 0) << sh)
        setR(a[0], v)
    elif op in ('ldr', 'ldur'):
        b, off, wb = addr(a[1])
        setR(a[0], load(b, off, width(a[0])))
    elif op in ('ldp',):
        b, off, wb = addr(a[2])
        w = width(a[0])
        setR(a[0], load(b, off, w))
        setR(a[1], load(b, off + w, w))
    elif op in ('str', 'stur'):
        b, off, wb = addr(a[1])
        store(b, off, R(a[0]), width(a[0]))
        if wb and b:
            setR(a[1].strip('[]!').split(',')[0], (b[0], b[1] + off))
    elif op == 'stp':
        b, off, wb = addr(a[2])
        w = width(a[0])
        store(b, off, R(a[0]), w)
        store(b, off + w, R(a[1]), w)
        if wb and b:
            setR(a[2].strip('[]!').split(',')[0], (b[0], b[1] + off))
    elif op == 'strh':
        b, off, wb = addr(a[1]); store(b, off, R(a[0]), 2)
    elif op == 'strb':
        b, off, wb = addr(a[1]); store(b, off, R(a[0]), 1)
    elif op in ('stp',):
        pass
    elif op in ('sxtw', 'sub'):
        setR(a[0], None)
    else:
        sys.stderr.write('unhandled %s %s\n' % (op, args))
lo, hi = min(mem), max(mem) + 1
buf = bytearray(hi - lo)
miss = [i for i in range(lo, hi) if i not in mem]
for k, v in mem.items():
    buf[k - lo] = v
sys.stderr.write('range %#x-%#x missing %d bytes %s\n' % (lo, hi, len(miss), miss[:10]))
sys.stdout.write(buf.decode('latin-1'))
