#!/usr/bin/env python3
"""symcmp.py <stock.ko> <ours.ko> func... [-v]
Symbolic comparison of one function in two builds, independent of register allocation, stack layout and block placement.
Per build: CFG from the listing; forward data flow with symbolic values (args a0..a7, loads ld<kind>(addr), calls ret(fn,args),
constants, ops; stack slots tracked so spills/fills vanish; merges -> phi{non-phi incomings}). Emitted FACTS:
  call(fn, args[:arity])  st(kind, addr, val) to non-stack memory  stkarg(fn, reloff, kind, val) stack data passed by pointer
  cond(canonical compare)  trap(imm)  ret(x0)
Canonical compares: operand order sorted (cond swapped), cmp #N gt/le/hi/ls -> #N+1 ge/lt/hs/lo, cbz/cbnz/tbz sign-bit/tst single bit
folded, polarity dropped (class only). Must-guards: per block the set of branch literals true on every path; per fact the
intersection over its occurrences. Report: facts only in one build (set), multiplicity changes, and guard-set differences."""
import re, sys, subprocess, bisect, hashlib, collections
sys.path.insert(0, "<work-dir>")
from compare_ko_v2 import Elf

OBJDUMP = '<build-dir>'
HMAP = {}


def H(s):
    if len(s) <= 150:
        return s
    h = 'H' + hashlib.md5(s.encode()).hexdigest()[:8]
    HMAP[h] = s
    return h


def unH(v):
    return HMAP.get(v, v)


def canon_dp(base, w, srcs):
    """exact value-preserving canonicalisation of sign/zero-extension forms"""
    if base == 'sxtw' and len(srcs) == 1:
        m = re.fullmatch(r'ldr\.w\((.*)\)', unH(srcs[0]))
        if m:
            return H('ldrsw.x(%s)' % m.group(1))
    if base == 'and' and '4294967295' in srcs and len(srcs) == 2:
        o = srcs[1] if srcs[0] == '4294967295' else srcs[0]
        m = re.fullmatch(r'ldrsw\.x\((.*)\)', unH(o))
        if m:
            return H('ldr.w(%s)' % m.group(1))
        if unH(o).startswith('ldr.w(') or unH(o).startswith('ldrb.w(') or unH(o).startswith('ldrh.w('):
            return o
    return None


def listing(ko, fn, e):
    fs = [s for s in e.syms if s.name == fn and s.type == 2]
    if not fs:
        fs = [s for s in e.syms if re.sub(r'\.llvm\.\d+$', '', s.name) == fn and s.type == 2]
    f = fs[0]
    sh = f.shndx
    ro, rl = e.reloff.get(sh, []), e.rel.get(sh, [])
    rel = {}
    for j in range(bisect.bisect_left(ro, f.val), bisect.bisect_left(ro, f.val + f.size)):
        off, t, sy, add = rl[j]
        rel[off - f.val] = re.sub(r'\.llvm\.\d+', '', e.tgtp(sy, add)).replace('\n', '\\n')[:100]
    out = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', '--disassemble-symbols=' + f.name, ko],
                         capture_output=True, text=True).stdout.split('\n')
    base, rows = None, []
    for l in out:
        m = re.match(r'([0-9a-f]+) <(.+)>:', l)
        if m:
            base = int(m.group(1), 16) if m.group(2) == f.name else None
            continue
        m = re.match(r'\s+([0-9a-f]+):\s+(\S.*)$', l)
        if m and base is not None:
            a = int(m.group(1), 16) - base
            t = re.sub(r'\s*//.*$', '', m.group(2)).replace('\t', ' ').strip()
            t = re.sub(r'0x[0-9a-f]+ <[^>]*\+0x([0-9a-f]+)>', lambda mm: '@%x' % int(mm.group(1), 16), t)
            t = re.sub(r'0x[0-9a-f]+ <[^>+]*>', '@0', t)
            sp = t.split(' ', 1)
            rows.append((a, sp[0], split_ops(sp[1]) if len(sp) > 1 else [], rel.get(a)))
    return rows


def split_ops(s):
    ops, cur, depth = [], '', 0
    for ch in s:
        if ch == '[':
            depth += 1
        elif ch == ']':
            depth -= 1
        if ch == ',' and depth == 0:
            ops.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip():
        ops.append(cur.strip())
    # merge shift / extend modifiers into the previous operand
    out = []
    for o in ops:
        if re.match(r'^(lsl|lsr|asr|ror|uxt[bhwx]|sxt[bhwx])\b', o) and out:
            out[-1] = out[-1] + ' ' + o
        else:
            out.append(o)
    return out


REGRE = re.compile(r'^([wx])(\d+|zr)$')
SWAP = {'gt': 'lt', 'lt': 'gt', 'ge': 'le', 'le': 'ge', 'hi': 'lo', 'lo': 'hi', 'hs': 'ls', 'ls': 'hs', 'eq': 'eq', 'ne': 'ne',
        'cc': 'hi', 'cs': 'ls'}
INV = {'eq': 'ne', 'ne': 'eq', 'lt': 'ge', 'ge': 'lt', 'gt': 'le', 'le': 'gt', 'lo': 'hs', 'hs': 'lo', 'hi': 'ls', 'ls': 'hi',
       'mi': 'pl', 'pl': 'mi', 'vs': 'vc', 'vc': 'vs', 'z': 'nz', 'nz': 'z', 'al': 'nv'}
ALIAS = {'cc': 'lo', 'cs': 'hs'}
REP = {'eq': 'eq', 'ne': 'eq', 'lt': 'lt', 'ge': 'lt', 'gt': 'gt', 'le': 'gt', 'lo': 'lo', 'hs': 'lo', 'hi': 'hi', 'ls': 'hi',
       'mi': 'mi', 'pl': 'mi', 'vs': 'vs', 'vc': 'vs', 'z': 'z', 'nz': 'z'}
COMM = {'add', 'adds', 'mul', 'and', 'ands', 'orr', 'eor', 'smull', 'umull', 'smulh', 'umulh', 'cmn'}


def isint(v):
    return re.match(r'^-?\d+$', v) is not None


def lit(flags, cond):
    """canonical literal (key, truth) for a condition evaluated on flags"""
    cond = ALIAS.get(cond, cond)
    if flags is None:
        return ('?flags', cond == REP.get(cond, cond))
    kind = flags[0]
    if kind == 'cmp':
        _, w, a, b = flags
        if w == 'w':
            a, b = [H('ldr.w(%s)' % m.group(1)) if (m := re.fullmatch(r'ldrsw\.x\((.*)\)', unH(x))) else x for x in (a, b)]
        if cond not in SWAP:     # mi/pl/vs/vc: no operand swap possible
            pass
        elif isint(a) and not isint(b):
            a, b, cond = b, a, SWAP[cond]
        elif not isint(a) and not isint(b) and a > b:
            a, b, cond = b, a, SWAP[cond]
        if isint(b):
            n = int(b)
            if cond in ('gt', 'le', 'hi', 'ls'):
                n += 1
                cond = {'gt': 'ge', 'le': 'lt', 'hi': 'hs', 'ls': 'lo'}[cond]
            if n == 0 and cond in ('mi', 'pl'):
                cond = 'lt' if cond == 'mi' else 'ge'
            if n == 0 and cond == 'lo':      # unsigned < 0 never: keep
                pass
            b = str(n)
        key = 'cmp%s(%s,%s)' % (w, a, b)
    elif kind == 'tst':
        _, w, a, b = flags
        if isint(a):
            a, b = b, a
        if isint(b) and int(b) > 0 and (int(b) & (int(b) - 1)) == 0 and cond in ('eq', 'ne'):
            key = 'bit(%s,%d)' % (a, int(b).bit_length() - 1)
            cond = 'z' if cond == 'eq' else 'nz'
            if int(b) == 1 and unH(a).startswith('ldrb.w('):
                key = 'cmpw(%s,0)' % a
                cond = 'eq' if cond == 'z' else 'ne'
        else:
            key = 'tst%s(%s,%s)' % (w, *sorted((a, b)))
    elif kind == 'bit':
        key = 'bit(%s,%s)' % (flags[1], flags[2])
        if flags[2] == '0' and unH(flags[1]).startswith('ldrb.w(') and cond in ('z', 'nz'):
            key = 'cmpw(%s,0)' % flags[1]
            cond = 'eq' if cond == 'z' else 'ne'
    else:
        key = '%s(%s)' % (kind, ','.join(flags[1:]))
    rep = REP.get(cond, cond)
    return (H(key + ':' + rep), cond == rep)


class St:
    __slots__ = ('r', 'fl', 'stk', 'esc', 'lastcall')

    def __init__(self):
        self.r, self.fl, self.stk, self.esc, self.lastcall = {}, None, {}, set(), '-'

    def copy(self):
        n = St()
        n.r, n.fl, n.stk, n.esc, n.lastcall = dict(self.r), self.fl, dict(self.stk), set(self.esc), self.lastcall
        return n

    def key(self):
        return (tuple(sorted(self.r.items(), key=lambda kv: str(kv[0]))), self.fl, tuple(sorted(self.stk.items())), tuple(sorted(self.esc)), self.lastcall)


def merge_vals(vals):
    vals = [v for v in vals]
    if all(v == vals[0] for v in vals):
        return vals[0]
    inc = set()
    for v in vals:
        if v.startswith('phi{'):
            inc.update(v[4:-1].split('|') if v[4:-1] else [])
        elif 'phi{' not in v:
            inc.add(v)
    return H('phi{' + '|'.join(sorted(inc)) + '}')


def merge(states):
    states = [s for s in states if s is not None]
    if len(states) == 1:
        return states[0].copy()
    n = St()
    keys = set().union(*[s.r.keys() for s in states])
    for k in keys:
        n.r[k] = merge_vals([s.r.get(k, 'U') for s in states])
    fls = [s.fl for s in states]
    n.fl = fls[0] if all(f == fls[0] for f in fls) else ('phiflags',)
    keys = set().union(*[s.stk.keys() for s in states])
    for k in keys:
        n.stk[k] = merge_vals([s.stk.get(k, 'U') for s in states])
    n.esc = set().union(*[s.esc for s in states])
    lc = [s.lastcall for s in states]
    n.lastcall = lc[0] if all(x == lc[0] for x in lc) else 'phi'
    return n


SPRE = re.compile(r'^SP([+-]\d+)$')


def spoff(v):
    m = SPRE.match(v)
    return int(m.group(1)) if m else None


def stkn(v):
    return re.sub(r'SP[+-]\d+', 'STK', v)


class Fn:
    def __init__(self, rows, arity):
        self.rows, self.arity = rows, arity
        self.idx = {r[0]: i for i, r in enumerate(rows)}
        self.cfg()

    def cfg(self):
        rows = self.rows
        lead = {0}
        for i, (a, m, ops, rel) in enumerate(rows):
            t = self.target(m, ops, rel)
            if t is not None and t in self.idx:
                lead.add(self.idx[t])
            if self.isterm(m, ops, rel) and i + 1 < len(rows):
                lead.add(i + 1)
        L = sorted(lead)
        self.blocks = [(L[k], (L[k + 1] if k + 1 < len(L) else len(rows))) for k in range(len(L))]
        self.bstart = {s: b for b, (s, e) in enumerate(self.blocks)}
        self.succ = collections.defaultdict(list)
        self.brblocks = []
        for b, (s, e) in enumerate(self.blocks):
            a, m, ops, rel = rows[e - 1]
            t = self.target(m, ops, rel)
            tb = self.bstart.get(self.idx.get(t)) if t is not None else None
            nxt = b + 1 if e < len(rows) else None
            if m == 'b' and rel is None:
                self.succ[b] = [(tb, 'T')]
            elif m.startswith('b.') or m in ('cbz', 'cbnz', 'tbz', 'tbnz'):
                self.succ[b] = [(tb, 'T')] + ([(nxt, 'F')] if nxt is not None else [])
            elif m in ('ret', 'brk', 'udf', 'eret') or (m == 'b' and rel):
                self.succ[b] = []
            elif m == 'br':
                self.succ[b] = []
                self.brblocks.append(b)
            else:
                self.succ[b] = [(nxt, 'N')] if nxt is not None else []
        # jump tables: every block unreachable from the entry becomes a successor of the br blocks
        if self.brblocks:
            reach = self.reach()
            for b in range(len(self.blocks)):
                if b not in reach:
                    for bb in self.brblocks:
                        self.succ[bb].append((b, 'J'))
        self.pred = collections.defaultdict(list)
        for b, ss in self.succ.items():
            for (t, k) in ss:
                if t is not None:
                    self.pred[t].append((b, k))

    def reach(self):
        seen, st = {0}, [0]
        while st:
            b = st.pop()
            for t, k in self.succ[b]:
                if t is not None and t not in seen:
                    seen.add(t); st.append(t)
        return seen

    @staticmethod
    def target(m, ops, rel):
        if m == 'b' or m.startswith('b.') or m in ('cbz', 'cbnz', 'tbz', 'tbnz'):
            for o in ops[::-1]:
                mm = re.match(r'^@([0-9a-f]+)$', o)
                if mm:
                    return int(mm.group(1), 16)
        return None

    @staticmethod
    def isterm(m, ops, rel):
        return m in ('b', 'br', 'ret', 'brk', 'udf', 'cbz', 'cbnz', 'tbz', 'tbnz', 'eret') or m.startswith('b.')

    # ---- value helpers
    def val(self, st, o):
        o = o.strip()
        mm = re.match(r'^(\S+) (lsl|lsr|asr|ror|uxt[bhwx]|sxt[bhwx])(?: #(0x[0-9a-f]+|\d+))?$', o)
        if mm:
            v = self.val(st, mm.group(1))
            sh = int(mm.group(3), 0) if mm.group(3) else 0
            if mm.group(2) == 'lsl' and isint(v):
                return str(int(v) << sh)
            return H('%s(%s,%d)' % (mm.group(2), v, sh))
        if o.startswith('#'):
            try:
                return str(int(o[1:], 0))
            except ValueError:
                return o
        if o in ('sp', 'wsp'):
            return st.r.get('sp', 'SP+0')
        m = REGRE.match(o)
        if m:
            if m.group(2) == 'zr':
                return '0'
            return st.r.get(int(m.group(2)), 'U')
        return o

    def setr(self, st, o, v):
        if o in ('sp', 'wsp'):
            st.r['sp'] = v
            return
        m = REGRE.match(o)
        if m and m.group(2) != 'zr':
            st.r[int(m.group(2))] = v

    @staticmethod
    def w(o):
        return 'w' if o.startswith('w') else 'x'

    def addr(self, st, memop, rel):
        """returns (address value, writeback (reg, newval) or None) ; memop like '[x8, #0x10]!' plus optional post '#8'"""
        mm = re.match(r'^\[([^\]]+)\](!?)$', memop)
        inner = [x.strip() for x in mm.group(1).split(',')]
        base = self.val(st, inner[0])
        off = 0
        idx = None
        if len(inner) > 1:
            if inner[1].startswith('#'):
                off = int(inner[1][1:], 0)
            else:
                idx = self.val(st, ' '.join(inner[1:]).replace(', ', ' ')) if len(inner) > 2 else self.val(st, inner[1])
                if len(inner) > 2:
                    idx = self.val(st, inner[1] + ' ' + inner[2])
        if rel:
            a = 'ADDR(%s)' % rel
        elif idx is not None:
            a = H('%s+idx(%s)' % (base, idx))
        else:
            a = self.addk(base, off)
        wb = None
        if mm.group(2) == '!':
            wb = (inner[0], self.addk(base, off))
        return a, wb

    @staticmethod
    def addk(v, k):
        if k == 0:
            return v
        if isint(v):
            return str(int(v) + k)
        m = re.match(r'^(.*?)([+-]\d+)$', v)
        if m and (m.group(1) == 'SP' or not m.group(1).endswith(')') or True) and re.match(r'^(SP|.*\))$', m.group(1)):
            n = int(m.group(2)) + k
            return '%s%+d' % (m.group(1), n) if n else m.group(1)
        return '%s%+d' % (v, k)

    # ---- execute one block
    def run(self, b, st, facts=None):
        s, e = self.blocks[b]
        for i in range(s, e):
            self.step(self.rows[i], st, facts, b)
        return st

    def emit(self, facts, b, a, f):
        if facts is not None:
            facts.append((H(f), b, a))

    def step(self, row, st, facts, b):
        a, m, ops, rel = row
        V = lambda o: self.val(st, o)
        # stack-pointer bookkeeping in prologue/epilogue happens through generic add/sub on 'sp'
        if m in ('paciasp', 'autiasp', 'hint', 'nop', 'bti', 'dmb', 'dsb', 'isb', 'b') or m.startswith('b.') \
                or m in ('cbz', 'cbnz', 'tbz', 'tbnz', 'ret', 'br'):
            if m in ('dmb', 'dsb', 'isb'):
                self.emit(facts, b, a, 'barrier(%s %s)' % (m, ' '.join(ops)))
            if m == 'b' and rel:
                self.emit(facts, b, a, 'tailcall(%s)' % rel)
            if m.startswith('b.') or m in ('cbz', 'cbnz', 'tbz', 'tbnz'):
                self.emit(facts, b, a, 'cond(%s)' % self.brlit(st, m, ops)[0])
            if m == 'ret':
                self.emit(facts, b, a, 'ret(%s)' % stkn(st.r.get(0, 'U')))
            return
        if m in ('brk', 'udf'):
            self.emit(facts, b, a, 'trap(%s %s)' % (m, ops[0] if ops else ''))
            return
        if m in ('bl', 'blr'):
            fn = rel if (m == 'bl' and rel) else ('ind(%s)' % V(ops[0]) if m == 'blr' else 'bl?%s' % ops)
            n = self.arity.get(fn, 0)
            args = [stkn(st.r.get(k, 'U')) for k in range(n)]
            self.emit(facts, b, a, 'call(%s;%s)' % (fn, ';'.join(args)))
            # stack data passed by pointer
            bases = sorted({spoff(st.r.get(k, '')) for k in range(max(n, 1)) if spoff(st.r.get(k, '')) is not None})
            for k0 in bases:
                hi = min([x for x in bases if x > k0] + [k0 + 0x200])
                for (so, sw), sv in sorted(st.stk.items()):
                    if k0 <= so < hi and not re.search(r'in_x|SP_EL0|^U$|^stk\?$', sv):
                        self.emit(facts, b, a, 'stkarg(%s;%+d;%s;%s)' % (fn, so - k0, sw, stkn(sv)))
                st.esc.add(k0)
            ret = H('ret(%s;%s)' % (fn, ';'.join(args)))
            for k in range(19):
                st.r.pop(k, None)
            st.r[0] = ret
            st.r[1] = 'ret1(%s)' % ret
            st.fl = None
            # escaped stack regions may have been written by the callee
            for (so, sw) in list(st.stk):
                if any(so >= k0 for k0 in st.esc):
                    st.stk[(so, sw)] = 'out(%s)' % fn
            st.lastcall = fn
            return
        if m in ('stp', 'stnp', 'str', 'stur', 'strb', 'sturb', 'strh', 'sturh', 'stlr', 'stlrb', 'stlrh'):
            regs = ops[:2] if m in ('stp', 'stnp') else ops[:1]
            mem = ops[len(regs)]
            ad, wb = self.addr(st, mem, rel)
            post = ops[len(regs) + 1] if len(ops) > len(regs) + 1 else None
            sz = {'b': 1, 'h': 2}.get(m.rstrip('p')[-1] if m.endswith(('b', 'h')) else '', 0) or (4 if regs[0].startswith('w') else 8)
            kind = m.replace('stur', 'str').replace('stnp', 'stp') + ('' if m.endswith(('b', 'h')) else '.' + self.w(regs[0]))
            for j, rg in enumerate(regs):
                v = V(rg)
                aj = self.addk(ad, j * sz)
                so = spoff(aj)
                if so is not None:
                    st.stk[(so, sz)] = v
                    for (o2, w2) in list(st.stk):
                        if (o2, w2) != (so, sz) and o2 < so + sz and so < o2 + w2:
                            del st.stk[(o2, w2)]
                else:
                    if spoff(v) is not None:
                        st.esc.add(spoff(v))
                    self.emit(facts, b, a, 'st(%s;%s;%s)' % (kind, stkn(aj), stkn(v)))
            if wb:
                self.setr(st, wb[0], wb[1])
            if post:
                base = mem.strip('[]').split(',')[0]
                self.setr(st, base, self.addk(V(base), int(post[1:], 0)))
            return
        if m.startswith('ld') and len(ops) >= 2 and any(o.startswith('[') for o in ops):
            pair = m in ('ldp', 'ldpsw', 'ldnp')
            regs = ops[:2] if pair else ops[:1]
            mem = ops[len(regs)]
            ad, wb = self.addr(st, mem, rel)
            post = ops[len(regs) + 1] if len(ops) > len(regs) + 1 else None
            base_m = m.replace('ldur', 'ldr').replace('ldnp', 'ldp')
            if m.startswith(('ldadd', 'ldclr', 'ldset', 'ldeor', 'ldsmax', 'ldumax', 'ldaxr', 'ldxr', 'ldar', 'ldapr')):
                self.emit(facts, b, a, 'atomic(%s;%s;%s)' % (m, stkn(ad), ';'.join(stkn(V(o)) for o in ops[:-1])))
            sz = 1 if base_m.endswith(('b', 'sb')) or 'rb' in base_m or 'rsb' in base_m else 2 if base_m.endswith('h') else (4 if (regs[0].startswith('w') or base_m in ('ldrsw', 'ldpsw')) else 8)
            for j, rg in enumerate(regs):
                aj = self.addk(ad, j * sz)
                so = spoff(aj)
                if so is not None:
                    v = st.stk.get((so, sz))
                    if v is None:
                        v = next((vv for (o2, w2), vv in st.stk.items() if o2 == so), None)
                    if v is None:
                        v = ('out(%s)' % st.lastcall) if any(so >= k0 for k0 in st.esc) else 'stk?'
                else:
                    v = H('%s.%s(%s)' % (base_m, self.w(rg), stkn(aj)))
                self.setr(st, rg, v)
            if wb:
                self.setr(st, wb[0], wb[1])
            if post:
                base = mem.strip('[]').split(',')[0]
                self.setr(st, base, self.addk(V(base), int(post[1:], 0)))
            return
        if m == 'adrp':
            self.setr(st, ops[0], 'PAGE(%s)' % (rel or ops[1]))
            return
        if m == 'adr':
            self.setr(st, ops[0], 'ADR(%s)' % (rel or ops[1]))
            return
        if m in ('mov', 'movz') and len(ops) == 2:
            v = V(ops[1])
            self.setr(st, ops[0], v)
            return
        if m == 'movk':
            cur = V(ops[0])
            mm = re.match(r'#(0x[0-9a-f]+|\d+)(?:, lsl #(\d+))?', ', '.join(ops[1:]).replace(' lsl', ', lsl') if False else ops[1])
            imm = int(re.match(r'#(0x[0-9a-f]+|\d+)', ops[1]).group(1), 0)
            sh = int(re.search(r'lsl #(\d+)', ops[1]).group(1)) if 'lsl' in ops[1] else 0
            if isint(cur):
                c = int(cur) & ((1 << 64) - 1)
                c = (c & ~(0xffff << sh)) | (imm << sh)
                if self.w(ops[0]) == 'w':
                    c &= 0xffffffff
                    if c >= 1 << 31:
                        c -= 1 << 32
                elif c >= 1 << 63:
                    c -= 1 << 64
                self.setr(st, ops[0], str(c))
            else:
                self.setr(st, ops[0], H('movk(%s,%d,%d)' % (cur, imm, sh)))
            return
        if m == 'mrs':
            self.setr(st, ops[0], 'sysreg(%s)' % ops[1])
            return
        if m == 'msr':
            self.emit(facts, b, a, 'msr(%s;%s)' % (ops[0], V(ops[1])))
            return
        if m in ('cmp', 'cmn', 'tst'):
            x, y = V(ops[0]), V(ops[1])
            if m == 'cmn':
                if isint(y):
                    st.fl = ('cmp', self.w(ops[0]), x, str(-int(y)))
                else:
                    st.fl = ('cmn', self.w(ops[0]), *sorted((x, y)))
            else:
                st.fl = (m, self.w(ops[0]), x, y)
            return
        if m in ('ccmp', 'ccmn'):
            x, y = V(ops[0]), V(ops[1])
            cond = ops[3]
            pk, pt = lit(st.fl, cond)
            if m == 'ccmn' and isint(y):
                inner = lit(('cmp', self.w(ops[0]), x, str(-int(y))), 'eq')[0]
            else:
                inner = lit((('cmp' if m == 'ccmp' else 'cmn'), self.w(ops[0]), x, y), 'eq')[0]
            st.fl = ('ccmp', H('%s=%s' % (pk, pt)), inner.rsplit(':', 1)[0], ops[2])
            self.emit(facts, b, a, 'cond(%s)' % pk)
            return
        if m in ('csel', 'csinc', 'csinv', 'csneg', 'cset', 'csetm', 'cinc', 'cinv', 'cneg'):
            cond = ops[-1]
            k, t = lit(st.fl, cond)
            self.emit(facts, b, a, 'cond(%s)' % k)
            if m in ('cset', 'csetm'):
                tv, fv = ('1', '0') if m == 'cset' else ('-1', '0')
            elif m in ('cinc', 'cinv', 'cneg'):
                x = V(ops[1])
                tv, fv = H('%s(%s)' % (m, x)), x
            else:
                x, y = V(ops[1]), V(ops[2])
                tv, fv = x, (y if m == 'csel' else H('%s(%s)' % (m, y)))
            if not t:
                tv, fv = fv, tv
            self.setr(st, ops[0], H('ite(%s,%s,%s)' % (k, tv, fv)))
            return
        if m in ('add', 'sub', 'adds', 'subs') and len(ops) == 3 and ops[2].startswith('#'):
            x = V(ops[1])
            imm = int(re.match(r'#(0x[0-9a-f]+|\d+)', ops[2]).group(1), 0)
            if 'lsl #12' in ops[2]:
                imm <<= 12
            if m.startswith('sub'):
                imm = -imm
            if rel:
                v = 'ADDR(%s)' % rel
            else:
                v = self.addk(x, imm) if self.w(ops[0]) == 'x' or isint(x) else H('add.w(%s,%d)' % (x, imm))
            if m in ('adds', 'subs'):
                st.fl = ('cmp', self.w(ops[0]), x, str(-imm))
            self.setr(st, ops[0], v)
            return
        if m in ('subs', 'adds', 'ands') and len(ops) == 3:
            x, y = V(ops[1]), V(ops[2])
            st.fl = ('cmp' if m == 'subs' else 'cmn' if m == 'adds' else 'tst', self.w(ops[0]), x, y)
            if m == 'cmn' and False:
                pass
        # generic data processing
        if not ops:
            self.emit(facts, b, a, 'insn(%s)' % m)
            return
        dst = ops[0]
        srcs = [V(o) for o in ops[1:]]
        base = m.rstrip('s') if m in ('adds', 'subs', 'ands', 'bics') else m
        if base in COMM and len(srcs) == 2:
            srcs = sorted(srcs)
        if base == 'madd' or base == 'msub':
            srcs = sorted(srcs[:2]) + srcs[2:]
        if base in ('add',) and len(srcs) == 2 and spoff(srcs[0]) is not None and isint(srcs[1]):
            v = self.addk(srcs[0], int(srcs[1]))
        else:
            v = canon_dp(base, self.w(dst), srcs) or H('%s.%s(%s)' % (base, self.w(dst), ','.join(srcs)))
        if REGRE.match(dst) or dst in ('sp', 'wsp'):
            self.setr(st, dst, v)
        else:
            self.emit(facts, b, a, 'insn(%s %s)' % (m, ','.join(ops)))

    def brlit(self, st, m, ops):
        """literal that is TRUE when the branch is taken"""
        if m.startswith('b.'):
            return lit(st.fl, m[2:])
        x = self.val(st, ops[0])
        w = self.w(ops[0])
        if m in ('cbz', 'cbnz'):
            return lit(('cmp', w, x, '0'), 'eq' if m == 'cbz' else 'ne')
        bit = int(ops[1][1:], 0)
        if (w == 'w' and bit == 31) or (w == 'x' and bit == 63):
            return lit(('cmp', w, x, '0'), 'ge' if m == 'tbz' else 'lt')
        return lit(('bit', x, str(bit)), 'z' if m == 'tbz' else 'nz')

    def analyse(self):
        nb = len(self.blocks)
        ins = [None] * nb
        e0 = St()
        for k in range(8):
            e0.r[k] = 'a%d' % k
        for k in range(19, 30):
            e0.r[k] = 'in_x%d' % k
        e0.r['sp'] = 'SP+0'
        outs = [None] * nb
        order = list(range(nb))
        for it in range(60):
            changed = False
            for b in order:
                if b == 0:
                    sin = e0.copy()
                else:
                    ps = [outs[p] for p, k in self.pred[b] if outs[p] is not None]
                    if not ps:
                        continue
                    sin = merge(ps)
                if ins[b] is not None and ins[b].key() == sin.key():
                    continue
                ins[b] = sin
                outs[b] = self.run(b, sin.copy())
                changed = True
            if not changed:
                break
        self.ins = ins
        facts = []
        self.outfl = {}
        for b in range(nb):
            if ins[b] is None:
                continue
            st = ins[b].copy()
            s, e = self.blocks[b]
            for i in range(s, e):
                if i == e - 1:
                    self.lastst = st.copy()
                self.step(self.rows[i], st, facts, b)
            self.outfl[b] = self.lastst
        # must-guards
        lits = {}
        for b in range(nb):
            if ins[b] is None:
                continue
            a, m, ops, rel = self.rows[self.blocks[b][1] - 1]
            if m.startswith('b.') or m in ('cbz', 'cbnz', 'tbz', 'tbnz'):
                k, t = self.brlit(self.outfl[b], m, ops)
                lits[(b, 'T')] = (k, t)
                lits[(b, 'F')] = (k, not t)
        G = [None] * nb
        G[0] = frozenset()
        for it in range(200):
            ch = False
            for b in range(1, nb):
                cur = None
                for p, k in self.pred[b]:
                    if G[p] is None:
                        continue
                    g = set(G[p])
                    if (p, k) in lits:
                        g.add(lits[(p, k)])
                    cur = g if cur is None else (cur & g)
                if cur is not None:
                    cur = frozenset(cur)
                    if cur != G[b]:
                        G[b] = cur; ch = True
            if not ch:
                break
        self.G = G
        return facts


def arity_pass(rows, ar):
    cur = set()
    for a, m, ops, rel in rows:
        if m in ('bl', 'blr'):
            fn = rel if (m == 'bl' and rel) else None
            if m == 'blr':
                continue
            n = max([k + 1 for k in cur if k < 8] + [0])
            ar[fn] = max(ar.get(fn, 0), n)
            cur = set()
            continue
        if Fn.isterm(m, ops, rel):
            cur = set()
            continue
        if ops and not m.startswith(('st', 'cmp', 'cmn', 'tst', 'b', 'cb', 'tb')):
            regs = ops[:2] if m in ('ldp', 'ldpsw') else ops[:1]
            for o in regs:
                mm = REGRE.match(o)
                if mm and mm.group(2) != 'zr':
                    cur.add(int(mm.group(2)))


EQRE = re.compile(r'^(cmp[wx])\((.*),(-?\d+)\):eq$')


def reduce_guards(g):
    """drop NOT(X==c2) literals implied by a positive X==c1 (c1 != c2): mutually exclusive switch cases"""
    pos = {}
    for k, t in g:
        m = EQRE.match(unH(k))
        if m and t:
            pos[(m.group(1), m.group(2))] = m.group(3)
    out = set()
    for k, t in g:
        m = EQRE.match(unH(k))
        if m and not t and (m.group(1), m.group(2)) in pos and pos[(m.group(1), m.group(2))] != m.group(3):
            continue
        out.add((k, t))
    return frozenset(out)


def summarize(fn, facts):
    by = collections.defaultdict(list)
    for f, b, a in facts:
        by[f].append((b, a))
    out = {}
    for f, occ in by.items():
        g = None
        for b, a in occ:
            gb = fn.G[b] if fn.G[b] is not None else frozenset()
            g = set(gb) if g is None else (g & gb)
        out[f] = (len(occ), reduce_guards(frozenset(g or ())), sorted(a for b, a in occ))
    return out


def full(v):
    prev = None
    while prev != v:
        prev = v
        v = re.sub(r'H[0-9a-f]{8}', lambda m: HMAP.get(m.group(0), m.group(0)), v)
    return v


def nphi(v):
    v = full(v)
    prev = None
    while prev != v:
        prev = v
        v = re.sub(r'phi\{[^{}]*\}', 'PHI', v)
    v = re.sub(r'out\([^()]*\)', 'OUT', v)
    v = re.sub(r'ret1\(', 'RET1(', v)
    v = re.sub(r'in_x\d+', 'IN', v)
    return v


def fmt(f):
    return re.sub(r'H[0-9a-f]{8}', lambda m: '<' + HMAP.get(m.group(0), m.group(0))[:90] + '>', f)


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('-')]
    verbose = '-v' in sys.argv
    sk, ok = args[0], args[1]
    es, eo = Elf(sk), Elf(ok)
    for fname in args[2:]:
        rs, ro = listing(sk, fname, es), listing(ok, fname, eo)
        ar = {}
        arity_pass(rs, ar)
        arity_pass(ro, ar)
        fs, fo = Fn(rs, ar), Fn(ro, ar)
        S, O = summarize(fs, fs.analyse()), summarize(fo, fo.analyse())
        so = sorted(set(S) - set(O), key=lambda f: S[f][2][0])
        oo = sorted(set(O) - set(S), key=lambda f: O[f][2][0])
        cnt = [f for f in set(S) & set(O) if S[f][0] != O[f][0]]
        gd = []
        for f in set(S) & set(O):
            if S[f][1] != O[f][1]:
                gd.append(f)
        print('=== %s: stock %d insns/%d blocks, ours %d/%d; facts %d/%d; only-stock %d, only-ours %d, count-diff %d, guard-diff %d'
              % (fname, len(rs), len(fs.blocks), len(ro), len(fo.blocks), len(S), len(O), len(so), len(oo), len(cnt), len(gd)))
        for f in so:
            print('  - S@%s %s' % ('/'.join('%x' % x for x in S[f][2][:4]), fmt(f)))
        for f in oo:
            print('  + O@%s %s' % ('/'.join('%x' % x for x in O[f][2][:4]), fmt(f)))
        EFF = re.compile(r'^(call|st|stkarg|atomic|msr|tailcall|trap|barrier|insn)\(')
        iseff = lambda f: EFF.match(unH(f)) is not None
        e_so = [f for f in so if iseff(f)]; e_oo = [f for f in oo if iseff(f)]
        e_cnt = [f for f in cnt if iseff(f)]
        e_gd = [f for f in gd if iseff(f)]
        r_d = [f for f in so + oo if unH(f).startswith('ret(')]
        c_d = [f for f in so + oo if unH(f).startswith('cond(')]
        print('  SUMMARY %s: effects only-stock %d only-ours %d count %d guard %d | ret-diff %d cond-form-diff %d => %s' % (
            fname, len(e_so), len(e_oo), len(e_cnt), len(e_gd), len(r_d), len(c_d),
            'REVIEW' if (e_so or e_oo or e_cnt or e_gd or r_d) else ('COND-FORM' if c_d else 'CLASS-A-auto')))
        # stkarg with call-result labels normalised
        def SK(D):
            r = collections.Counter()
            for f in D:
                u = unH(f)
                if u.startswith('stkarg('):
                    r[re.sub(r'out\([^()]*\)', 'OUT', full(u))] += 1
            return r
        kS, kO = SK(S), SK(O)
        print('  SUMMARY-STK %s: stkarg only-stock %d only-ours %d (labels normalised)' % (fname, sum((kS - kO).values()), sum((kO - kS).values())))
        for k, c in list((kS - kO).items())[:6]:
            print('   ~ STK S-only', k[:230])
        for k, c in list((kO - kS).items())[:6]:
            print('   ~ STK O-only', k[:230])
        # phi-insensitive second opinion (jump-table / loop phi noise)
        def N(D):
            r = {}
            for f, (c, g, a) in D.items():
                if iseff(f):
                    k = nphi(f)
                    gg = frozenset((nphi(x), t) for x, t in g)
                    ints = frozenset(re.findall(r'(?<![\w.])-?\d+(?![\w.])', full(f)))
                    r.setdefault(k, []).append((c, gg, ints))
            return r
        import os
        if os.environ.get('SKIPPHI'):
            continue_phi = False
            NS, NO = {}, {}
        else:
            NS, NO = N(S), N(O)
        n_so = sorted(set(NS) - set(NO)); n_oo = sorted(set(NO) - set(NS))
        n_gd = [k for k in set(NS) & set(NO) if sorted([x[:2] for x in NS[k]], key=repr) != sorted([x[:2] for x in NO[k]], key=repr)]
        n_cd = [k for k in set(NS) & set(NO) if frozenset().union(*[x[2] for x in NS[k]]) != frozenset().union(*[x[2] for x in NO[k]])]
        print('  SUMMARY-PHI %s: effects only-stock %d only-ours %d guard-or-count %d => %s' % (
            fname, len(n_so), len(n_oo), len(n_gd) + 1000 * 0, 'CLASS-A-phi' if not (n_so or n_oo or n_gd or r_d or n_cd) else 'REVIEW'))
        for k in n_cd[:12]:
            print('   ~ CONST-DIFF', k[:200], sorted(frozenset().union(*[x[2] for x in NS[k]]) ^ frozenset().union(*[x[2] for x in NO[k]])))
        for k in n_so[:12]:
            print('   ~ S-only', k[:300])
        for k in n_oo[:12]:
            print('   ~ O-only', k[:300])
        for k in n_gd[:12]:
            print('   ~ GUARD', k[:200])
        if verbose:
            for f in sorted(cnt, key=lambda f: S[f][2][0]):
                print('  # count S%d@%s O%d@%s %s' % (S[f][0], '/'.join('%x' % x for x in S[f][2][:4]), O[f][0],
                                                       '/'.join('%x' % x for x in O[f][2][:4]), fmt(f)[:160]))
        for f in sorted(gd, key=lambda f: S[f][2][0]):
            a, b = S[f][1] - O[f][1], O[f][1] - S[f][1]
            print('  G S@%s O@%s %s' % ('/'.join('%x' % x for x in S[f][2][:3]), '/'.join('%x' % x for x in O[f][2][:3]), fmt(f)[:200]))
            for k, t in sorted(a):
                print('      stock-only guard: %s%s' % ('' if t else 'NOT ', fmt(k)[:220]))
            for k, t in sorted(b):
                print('      ours-only  guard: %s%s' % ('' if t else 'NOT ', fmt(k)[:220]))


if __name__ == '__main__':
    main()
