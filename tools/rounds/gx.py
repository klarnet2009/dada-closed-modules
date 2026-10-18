"""gx.py: concrete mini-emulator of aarch64 objdump text, records stores into a struct pointed to by x0 ('dev').
usage: gx.py FILE FUNC [chip] [--cmm=FILE2:FUNC2] [--stop=name,name] [--layout=layout.json]  -> prints events + final word map
FILE: objdump -dr text (stock dis.txt or llvm-objdump -dr --no-show-raw-insn of the .o).  chip: value of dev+4 (default 0).
As a module: run(file, func, chip, init_mem=None) -> (events, mem) ; fields(mem) -> {off: ('w', val) | ('p', sym)}."""
import re, sys, json

M64 = (1 << 64) - 1
RODATA = None
READS = {}


def load(path):
    """-> funcs: name -> list of (addr, text, relocs[(type,sym)]) in order"""
    funcs, cur = {}, None
    for l in open(path, errors='replace'):
        l = l.rstrip('\n')
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:\s*$', l)
        if m:
            cur = re.sub(r'\.llvm\.\d+$', '', m.group(1))
            funcs[cur] = []
            continue
        if cur is None:
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+R_AARCH64_(\w+)\s+(\S+)', l)
        if not m:
            m = re.match(r'^\s+[0-9a-f]+:\s+R_AARCH64_(\w+)\s+(\S+)', l.replace('\t\t', '    '))
        if m:
            if funcs[cur]:
                funcs[cur][-1][2].append((m.group(1), m.group(2)))
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(?:[0-9a-f]{8}\s+)?(\S.*)$', l)
        if m and not m.group(2).startswith('R_AARCH64') and not m.group(2).startswith('.word'):
            funcs[cur].append((int(m.group(1), 16), re.sub(r'\s*//.*$', '', m.group(2)).strip(), []))
    return funcs


def split_ops(s):
    out, d, c = [], 0, ''
    for ch in s:
        if ch == '[':
            d += 1
        if ch == ']':
            d -= 1
        if ch == ',' and d == 0:
            out.append(c.strip())
            c = ''
        else:
            c += ch
    if c.strip():
        out.append(c.strip())
    return out


def num(s):
    return int(s.lstrip('#'), 0)


class Emu:
    def __init__(self, funcs, func, chip=0, mem=None, stop=(), cmm=None, verbose=False):
        self.f = funcs[func]
        self.idx = {a: i for i, (a, _, _) in enumerate(self.f)}
        self.r = {}
        self.r['x0'] = ('dev', 0)
        self.flags = None
        self.ev = []           # (kind, off, size, val)
        self.mem = dict(mem or {})  # off -> ('b', byte) ...  stored as bytes dict & ptr dict
        self.ptr = {}
        self.chip = chip
        self.stop = set(stop)
        self.cmm = cmm
        self.verbose = verbose
        self.calls = []
        self.stk = {}
        self.reads = READS

    # register access
    def rd(self, n):
        if n in ('xzr', 'wzr'):
            return 0
        if n == 'sp':
            return ('stk', 0)
        v = self.r.get('x' + n[1:])
        if v is None:
            return None
        if n[0] == 'w' and isinstance(v, int):
            return v & 0xffffffff
        return v

    def wr(self, n, v):
        if n in ('xzr', 'wzr', 'sp'):
            return
        if n[0] == 'w' and isinstance(v, int):
            v &= 0xffffffff
        elif n[0] == 'w':
            v = None
        self.r['x' + n[1:]] = v

    def store(self, base, off, size, v):
        if isinstance(base, tuple) and base[0] == 'stk':
            o = base[1] + off
            for i in range(size):
                if isinstance(v, int):
                    self.stk[o + i] = (v >> (8 * i)) & 0xff
                else:
                    self.stk.pop(o + i, None)
            return
        if base is None or not isinstance(base, tuple) or base[0] != 'dev':
            return
        o = base[1] + off
        if isinstance(v, int):
            for i in range(size):
                self.mem[o + i] = (v >> (8 * i)) & 0xff
            for k in list(self.ptr):
                if o <= k < o + size or k <= o < k + 8:
                    del self.ptr[k]
        elif isinstance(v, tuple) and v[0] == 'sym' and size == 8:
            for i in range(size):
                self.mem.pop(o + i, None)
            self.ptr[o] = v[1]
        else:
            for i in range(size):
                self.mem.pop(o + i, None)
        self.ev.append(('st', o, size, v))

    def load(self, base, off, size):
        if isinstance(base, tuple) and base[0] == 'stk':
            o = base[1] + off
            if all(o + i in self.stk for i in range(size)):
                return sum(self.stk[o + i] << (8 * i) for i in range(size))
            return None
        if isinstance(base, tuple) and base[0] == 'sym' and RODATA is not None:
            m = re.match(r'\.rodata\+0x([0-9a-f]+)$', base[1])
            if m and int(m.group(1), 16) + off + size <= len(RODATA):
                o = int(m.group(1), 16) + off
                return int.from_bytes(RODATA[o:o + size], 'little')
            return None
        if not (isinstance(base, tuple) and base[0] == 'dev'):
            return None
        o = base[1] + off
        if o == 4 and size == 4 and 4 not in self.mem:
            return self.chip
        if size == 8 and o in self.ptr:
            return ('sym', self.ptr[o])
        v = 0
        for i in range(size):
            if o + i not in self.mem:
                return None
            v |= self.mem[o + i] << (8 * i)
        return v

    def addr(self, s):
        m = re.match(r'\[(\w+)(?:,\s*#(-?(?:0x[0-9a-f]+|\d+)))?\](!?)$', s)
        if not m:
            m2 = re.match(r'\[(\w+)\],\s*#(-?(?:0x[0-9a-f]+|\d+))$', s)
            if m2:
                b = self.rd(m2.group(1))
                if isinstance(b, tuple) and b[0] in ('dev', 'stk'):
                    self.wr(m2.group(1), (b[0], b[1] + int(m2.group(2), 0)))
                return b, 0
            return None, 0
        b = self.rd(m.group(1))
        off = int(m.group(2), 0) if m.group(2) else 0
        if m.group(3) == '!':
            if isinstance(b, tuple) and b[0] in ('dev', 'stk'):
                nb = (b[0], b[1] + off)
                self.wr(m.group(1), nb)
                return nb, 0
            self.wr(m.group(1), None)
            return None, 0
        return b, off

    def cond(self, c):
        if self.flags is None:
            return None
        n, z, cc, v = self.flags
        t = {'eq': z, 'ne': not z, 'hs': cc, 'cs': cc, 'lo': not cc, 'cc': not cc, 'mi': n, 'pl': not n, 'vs': v, 'vc': not v,
             'hi': cc and not z, 'ls': (not cc) or z, 'ge': n == v, 'lt': n != v, 'gt': (not z) and n == v, 'le': z or n != v, 'al': True}
        return bool(t[c])

    def setcmp(self, a, b, w):
        if not isinstance(a, int) or not isinstance(b, int):
            self.flags = None
            return
        bits = 32 if w else 64
        mask = (1 << bits) - 1
        a &= mask
        b &= mask
        res = (a - b) & mask
        sa = a - (1 << bits) if a >> (bits - 1) else a
        sb = b - (1 << bits) if b >> (bits - 1) else b
        n = bool(res >> (bits - 1))
        z = res == 0
        c = a >= b
        v = not (-(1 << (bits - 1)) <= sa - sb < (1 << (bits - 1)))
        self.flags = (n, z, c, v)

    def opval(self, s, w):
        if s.startswith('#'):
            return num(s)
        return self.rd(s)

    def run(self, limit=20000):
        i = 0
        steps = 0
        while i < len(self.f) and steps < limit:
            steps += 1
            addr, txt, rel = self.f[i]
            parts = txt.split(None, 1)
            op = parts[0]
            a = split_ops(parts[1]) if len(parts) > 1 else []
            nxt = i + 1
            if self.verbose:
                print('%x %s' % (addr, txt))
            try:
                if op in ('paciasp', 'autiasp', 'nop', 'hint', 'dmb', 'isb'):
                    pass
                elif op == 'mov':
                    self.wr(a[0], num(a[1]) & (M64 if a[0][0] == 'x' else 0xffffffff) if a[1].startswith('#') else self.rd(a[1]))
                elif op == 'movk':
                    sh = int(a[2].split('#')[1], 0) if len(a) > 2 else 0
                    v = self.rd(a[0])
                    if isinstance(v, int):
                        self.wr(a[0], (v & ~(0xffff << sh)) | (num(a[1]) << sh))
                    else:
                        self.wr(a[0], None)
                elif op == 'adrp':
                    sym = [s for t, s in rel if t.startswith('ADR_PREL_PG')]
                    self.wr(a[0], ('sym', sym[0]) if sym else ('sym', '?'))
                elif op == 'adr':
                    self.wr(a[0], ('sym', '?'))
                elif op in ('add', 'sub'):
                    base = self.rd(a[1])
                    if a[2].startswith('#'):
                        imm = num(a[2])
                        if len(a) > 3:
                            imm <<= int(a[3].split('#')[1], 0)
                        if op == 'sub':
                            imm = -imm
                        if rel and isinstance(base, tuple) and base[0] == 'sym':
                            self.wr(a[0], ('sym', rel[0][1]))
                        elif isinstance(base, tuple) and base[0] in ('dev', 'stk'):
                            self.wr(a[0], (base[0], base[1] + imm))
                        elif isinstance(base, int):
                            self.wr(a[0], (base + imm) & M64)
                        else:
                            self.wr(a[0], None)
                    else:
                        b2 = self.rd(a[2])
                        if isinstance(base, int) and isinstance(b2, int):
                            self.wr(a[0], (base + b2 if op == 'add' else base - b2) & M64)
                        else:
                            self.wr(a[0], None)
                elif op in ('orr', 'and', 'eor', 'lsl', 'lsr'):
                    x = self.rd(a[1])
                    y = self.opval(a[2], a[0][0] == 'w') if len(a) > 2 else None
                    if isinstance(x, int) and isinstance(y, int):
                        f = {'orr': lambda p, q: p | q, 'and': lambda p, q: p & q, 'eor': lambda p, q: p ^ q, 'lsl': lambda p, q: p << q, 'lsr': lambda p, q: p >> q}[op]
                        self.wr(a[0], f(x, y) & M64)
                    else:
                        self.wr(a[0], None)
                elif op in ('str', 'stur', 'strh', 'strb', 'sturh', 'sturb'):
                    sz = {'strh': 2, 'strb': 1, 'sturh': 2, 'sturb': 1}.get(op, 4 if a[0][0] == 'w' else 8)
                    b, off = self.addr(a[1])
                    v = self.rd(a[0])
                    if isinstance(v, int):
                        v &= (1 << (8 * sz)) - 1
                    self.store(b, off, sz, v)
                elif op == 'stp':
                    sz = 4 if a[0][0] == 'w' else 8
                    b, off = self.addr(a[2])
                    self.store(b, off, sz, self.rd(a[0]))
                    self.store(b, off + sz, sz, self.rd(a[1]))
                elif op in ('ldr', 'ldur', 'ldrh', 'ldrb', 'ldurh', 'ldurb', 'ldrsw'):
                    sz = {'ldrh': 2, 'ldrb': 1, 'ldurh': 2, 'ldurb': 1, 'ldrsw': 4}.get(op, 4 if a[0][0] == 'w' else 8)
                    b, off = self.addr(a[1])
                    self.wr(a[0], self.load(b, off, sz))
                elif op == 'ldp':
                    sz = 4 if a[0][0] == 'w' else 8
                    b, off = self.addr(a[2])
                    self.wr(a[0], self.load(b, off, sz))
                    self.wr(a[1], self.load(b, off + sz, sz))
                elif op in ('cmp', 'cmn'):
                    x = self.rd(a[0])
                    y = self.opval(a[1], a[0][0] == 'w')
                    if op == 'cmn' and isinstance(y, int):
                        y = (-y) & M64
                    self.setcmp(x, y, a[0][0] == 'w')
                elif op == 'subs':
                    x = self.rd(a[1])
                    y = self.opval(a[2], a[0][0] == 'w')
                    self.setcmp(x, y, a[0][0] == 'w')
                    self.wr(a[0], (x - y) & M64 if isinstance(x, int) and isinstance(y, int) else None)
                elif op in ('tst', 'ccmp', 'ccmn', 'fcmp'):
                    self.flags = None
                elif op == 'cset':
                    c = self.cond(a[1])
                    self.wr(a[0], None if c is None else int(c))
                elif op == 'csel':
                    c = self.cond(a[3])
                    self.wr(a[0], None if c is None else (self.rd(a[1]) if c else self.rd(a[2])))
                elif op == 'b':
                    nxt = self.idx[int(a[0].split()[0], 16)]
                elif op.startswith('b.'):
                    c = self.cond(op[2:])
                    t = self.idx[int(a[0].split()[0], 16)]
                    if c is None:
                        # kCFI check: fallthrough is brk -> take the branch
                        if i + 1 < len(self.f) and self.f[i + 1][1].startswith('brk'):
                            c = True
                        else:
                            print('UNKNOWN COND at %x: %s' % (addr, txt))
                            return 'unknown-cond@%x' % addr
                    if c:
                        nxt = t
                elif op in ('cbz', 'cbnz'):
                    v = self.rd(a[0])
                    t = self.idx[int(a[1].split()[0], 16)]
                    if not isinstance(v, int):
                        print('UNKNOWN CBZ at %x: %s' % (addr, txt))
                        return 'unknown-cbz@%x' % addr
                    if (v == 0) == (op == 'cbz'):
                        nxt = t
                elif op in ('tbz', 'tbnz'):
                    v = self.rd(a[0])
                    t = self.idx[int(a[2].split()[0], 16)]
                    if not isinstance(v, int):
                        print('UNKNOWN TBZ at %x: %s' % (addr, txt))
                        return 'unknown-tbz@%x' % addr
                    bit = (v >> num(a[1])) & 1
                    if (bit == 0) == (op == 'tbz'):
                        nxt = t
                elif op == 'bl':
                    tgt = rel[0][1] if rel else '?'
                    tgt = re.sub(r'\.llvm\.\d+$', '', tgt)
                    args = [self.rd('x%d' % k) for k in range(4)]
                    self.calls.append((len(self.ev), tgt, args))
                    self.ev.append(('call', tgt, args))
                    if self.cmm and tgt == self.cmm[0]:
                        for k, v in self.cmm[1].items():
                            self.mem[k] = v
                        self.ptr.update(self.cmm[2])
                    for k in range(0, 18):
                        self.r.pop('x%d' % k, None)
                    self.r['x0'] = 0
                    if tgt in self.stop:
                        return 'stop@' + tgt
                elif op == 'blr':
                    args = [self.rd('x%d' % k) for k in range(4)]
                    self.ev.append(('call', 'INDIRECT', args))
                    if isinstance(args[2], tuple) and args[2][0] == 'stk' and isinstance(args[1], int) and args[1] in self.reads:
                        for k in range(4):
                            self.stk[args[2][1] + k] = (self.reads[args[1]] >> (8 * k)) & 0xff
                    for k in range(0, 18):
                        self.r.pop('x%d' % k, None)
                    self.r['x0'] = 0
                elif op in ('ret', 'retaa'):
                    return 'ret'
                elif op == 'brk':
                    return 'brk@%x' % addr
                elif op.startswith('b') and op != 'b':
                    pass
                else:
                    if a and re.match(r'^[xw]\d+$', a[0]) and not op.startswith('st') and op != 'cmp':
                        self.wr(a[0], None)
            except Exception as e:
                print('EMU ERROR at %x %s: %r' % (addr, txt, e))
                return 'error'
            i = nxt
        return 'end'


def run(file, func, chip=0, cmm=None, stop=(), verbose=False, funcs=None):
    funcs = funcs or load(file)
    e = Emu(funcs, func, chip, stop=stop, cmm=cmm, verbose=verbose)
    st = e.run()
    return e, st


def snapshot(e):
    return dict(e.mem), dict(e.ptr)


def words(mem, ptr):
    """final state -> {off: ('w', v) 4-byte aligned words fully known | ('p', sym)}"""
    out = {}
    offs = sorted(set(o // 4 * 4 for o in mem))
    for o in offs:
        if all(o + i in mem for i in range(4)):
            out[o] = ('w', sum(mem[o + i] << (8 * i) for i in range(4)))
        else:
            for i in range(4):
                if o + i in mem:
                    out[o + i] = ('b', mem[o + i])
    for o, s in ptr.items():
        out[o] = ('p', s)
        out.pop(o + 4, None)
    return out


if __name__ == '__main__':
    args = [x for x in sys.argv[1:] if not x.startswith('--')]
    opts = dict(x[2:].split('=', 1) if '=' in x else (x[2:], '1') for x in sys.argv[1:] if x.startswith('--'))
    if 'rodata' in opts:
        RODATA = open(opts['rodata'], 'rb').read()
    file, func = args[0], args[1]
    chip = int(args[2], 0) if len(args) > 2 else 0
    funcs = load(file)
    cmm = None
    if 'cmm' in opts:
        e0, st0 = run(file, opts['cmm'], 0, funcs=funcs)
        cmm = (opts['cmm'], e0.mem, e0.ptr)
        print('cmm:', st0, len(e0.ev), 'stores')
    e, st = run(file, func, chip, cmm=cmm, stop=opts.get('stop', '').split(',') if 'stop' in opts else (), verbose='v' in opts, funcs=funcs)
    print('status:', st, 'events:', len(e.ev))
    if 'ev' in opts:
        for x in e.ev:
            print(x)
    for o, v in sorted(words(e.mem, e.ptr).items()):
        print('+0x%x %s %s' % (o, v[0], ('0x%x' % v[1]) if v[0] != 'p' else v[1]))
