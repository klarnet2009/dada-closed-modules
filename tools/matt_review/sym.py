#!/usr/bin/env python3
"""sym.py stock.ko built.o stock.dis built.dis [func...]: register-allocation/scheduling/layout-insensitive comparison.
Per function: basic blocks + forward dataflow of SYMBOLIC expressions (registers, stack slots; loads keyed by address expression;
commutative operands sorted; compare operands canonicalised with the condition; stack offsets folded), then the multiset of
observable EVENTS (non-stack stores, calls with their argument expressions, conditional-branch predicates, returns, traps,
barriers, atomics, acquire loads) of stock vs built.  Prints the events that exist on one side only.
Limits: no memory SSA (a load is the same value wherever it is), PHI = unordered set of incoming values, call args = registers
written since the last call (x0..x7) + stack slots stored in the current block, branch direction is not compared (a predicate
and its negation are the same event), loop-carried values are '?'."""
import collections, hashlib, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('rvm', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rv_lib.py'))
rv = importlib.util.module_from_spec(spec); spec.loader.exec_module(rv)

E = {}          # hash -> (op tuple of child hashes/atoms)
W = {}          # hash -> width 32/64


def mk(op, *args, w=64):
    t = (op,) + tuple(args) + ((('w32',) if w == 32 else ()) if op not in ('c',) else ())
    h = hashlib.sha1(repr(t).encode()).hexdigest()[:12]
    if h not in E:
        E[h] = t
        W[h] = w
    return h


COMM = {'add', 'and', 'orr', 'eor', 'mul', 'umull', 'smull', 'umulh', 'smulh'}
INV = {'eq': 'ne', 'ne': 'eq', 'hs': 'lo', 'lo': 'hs', 'cs': 'cc', 'cc': 'cs', 'mi': 'pl', 'pl': 'mi', 'vs': 'vc', 'vc': 'vs',
       'hi': 'ls', 'ls': 'hi', 'ge': 'lt', 'lt': 'ge', 'gt': 'le', 'le': 'gt', 'al': 'nv', 'nv': 'al'}
SWAP = {'eq': 'eq', 'ne': 'ne', 'hs': 'ls', 'ls': 'hs', 'lo': 'hi', 'hi': 'lo', 'cs': 'ls', 'cc': 'hi', 'ge': 'le', 'le': 'ge',
        'lt': 'gt', 'gt': 'lt', 'mi': 'mi', 'pl': 'pl', 'vs': 'vs', 'vc': 'vc', 'al': 'al', 'nv': 'nv'}
NORMCC = {'cs': 'hs', 'cc': 'lo'}


def op2(op, a, b, w):
    if op in COMM and a > b:
        a, b = b, a
    return mk(op, a, b, w=w)


def const(v):
    return mk('c', v & 0xffffffffffffffff)


def cval(h):
    t = E.get(h)
    return t[1] if t and t[0] == 'c' else None


NEG = [False]
BRINFO = {}   # set by step(): predicate hash and 'taken when predicate true' flag of the last conditional branch


def cond_expr(cc, flags):
    """canonical predicate: (cc, a, b) with a<=b, negation folded"""
    cc = NORMCC.get(cc, cc)
    if flags is None:
        NEG[0] = INV[cc] < cc
        return mk('cond?', min(cc, INV[cc]))
    kind, w, a, b = flags
    if kind in ('cmp', 'cmn') and a > b and cc not in ('mi', 'pl', 'vs', 'vc'):
        a, b, cc = b, a, SWAP[cc]
    if kind == 'tst' and a > b:
        a, b = b, a
    ic = INV[cc]
    NEG[0] = False
    if ic < cc:
        cc = ic
        NEG[0] = True
    return mk('cond', kind, w, cc, a, b)


def pretty(h, d=4):
    t = E.get(h)
    if t is None:
        return str(h)
    if t[0] == 'c':
        return hex(t[1])
    if t[0] in ('sym', 'page', 'arg', 'clob', 'SA'):
        return '%s(%s)' % (t[0], t[1])
    if d == 0:
        return '..'
    return '%s(%s)' % (t[0], ','.join(pretty(x, d - 1) if isinstance(x, str) and len(x) == 12 and x in E else str(x) for x in t[1:]))


def coarse(h, d):
    t = E.get(h) if isinstance(h, str) else None
    if t is None:
        if isinstance(h, tuple):
            return '(' + ','.join(coarse(x, d) for x in h) + ')'
        return '*' if h in ('?', None) else str(h)
    k = t[0]
    if k == 'c':
        return hex(t[1])
    if k in ('sym', 'page'):
        return '%s(%s)' % (k, t[1])
    if k == 'sum':
        if d <= 0:
            return '~'
        parts = sorted(coarse(x, d - 1) for x in t[1])
        return 'sum(%s%s)' % (','.join(parts), (',' + hex(t[2])) if t[2] else '')
    if k in ('phi', 'clob', 'stk?', 'arg', 'ret', 'atomres', 'stxres', 'SA', 'SAany', 'undef', 'sysreg', 'adr'):
        return '*' if k not in ('sysreg', 'ret') else (k + '(' + str(t[1]) + ')')
    if d <= 0:
        return '~'
    return '%s(%s)' % (k, ','.join(coarse(x, d - 1) if (isinstance(x, str) and x in E) or isinstance(x, tuple) else str(x) for x in t[1:]))


REGRE = re.compile(r'^([xw])(\d+|zr)$|^(w?sp)$')


class St:
    def __init__(self):
        self.r = {}       # reg -> hash
        self.fresh = set()
        self.slot = {}    # stack offset -> hash
        self.sp = 0       # sp offset from entry
        self.flags = None


def run_fn(insns, tag):
    """insns: list of (off, mn, ops). returns Counter(events), descr"""
    offs = [i[0] for i in insns]
    idx = {o: k for k, o in enumerate(offs)}
    leaders = {0}
    for k, (o, mn, ops) in enumerate(insns):
        m = re.search(r'@(\d+)', ops)
        if m and rv.BR.match(mn) and mn != 'bl':
            t = int(m.group(1))
            if t in idx:
                leaders.add(idx[t])
            leaders.add(k + 1)
        if mn in ('ret', 'br', 'brk', 'b') or mn.startswith('b.') or mn in ('cbz', 'cbnz', 'tbz', 'tbnz'):
            leaders.add(k + 1)
    leaders = sorted(x for x in leaders if x < len(insns))
    blocks = []
    for i, s in enumerate(leaders):
        e = leaders[i + 1] if i + 1 < len(leaders) else len(insns)
        blocks.append((s, e))
    bstart = {s: bi for bi, (s, e) in enumerate(blocks)}
    succ = collections.defaultdict(list)
    for bi, (s, e) in enumerate(blocks):
        o, mn, ops = insns[e - 1]
        m = re.search(r'@(\d+)', ops)
        tgt = bstart.get(idx.get(int(m.group(1)), -1)) if m and rv.BR.match(mn) and mn != 'bl' else None
        if mn == 'b':
            if tgt is not None:
                succ[bi].append(tgt)
        elif mn.startswith('b.') or mn in ('cbz', 'cbnz', 'tbz', 'tbnz'):
            if tgt is not None:
                succ[bi].append(tgt)
            if bi + 1 < len(blocks):
                succ[bi].append(bi + 1)
        elif mn in ('ret', 'br', 'brk'):
            pass
        elif bi + 1 < len(blocks):
            succ[bi].append(bi + 1)
    preds = collections.defaultdict(list)
    for b, ss in succ.items():
        for x in ss:
            preds[x].append(b)
    # reverse postorder
    seen, order = set(), []

    def dfs(b):
        stack = [(b, 0)]
        seen.add(b)
        while stack:
            n, i = stack[-1]
            if i < len(succ[n]):
                stack[-1] = (n, i + 1)
                c = succ[n][i]
                if c not in seen:
                    seen.add(c)
                    stack.append((c, 0))
            else:
                order.append(n)
                stack.pop()
    dfs(0)
    for b in range(len(blocks)):
        if b not in seen:
            dfs(b)
    order.reverse()
    out = {}
    ev = collections.Counter()
    desc = {}
    callseq = [0]

    def emit(e, d):
        if not (E.get(e, ('',))[0] in ('BRC', 'BRT')):
            bev[curb[0]].append(e)
        if os.environ.get('COARSE'):
            e = coarse(e, int(os.environ.get('COARSE')))
            d = e
        ev[e] += 1
        desc[e] = d

    bev = collections.defaultdict(list)
    curb = [0]
    binfo = {}
    for b in order:
        curb[0] = b
        BRINFO.pop('last', None)
        st = St()
        ps = [out[p] for p in preds[b] if p in out]
        unk = any(p not in out for p in preds[b])
        if b == 0 and not preds[b]:
            for i in range(8):
                st.r['x%d' % i] = mk('arg', i)
                st.fresh.add('x%d' % i)
        elif ps:
            regs = set().union(*[set(p.r) for p in ps])
            for r in regs:
                vals = set(p.r.get(r, mk('clob', r)) for p in ps)
                if unk:
                    vals.add('?')
                st.r[r] = next(iter(vals)) if len(vals) == 1 else mk('phi', tuple(sorted(vals)))
            st.fresh = set.intersection(*[p.fresh for p in ps])
            sl = set().union(*[set(p.slot) for p in ps])
            for s in sl:
                vals = set(p.slot.get(s, '?') for p in ps)
                if unk:
                    vals.add('?')
                st.slot[s] = next(iter(vals)) if len(vals) == 1 else mk('phi', tuple(sorted(vals)))
            st.sp = ps[0].sp
            fl = set(p.flags for p in ps)
            st.flags = next(iter(fl)) if len(fl) == 1 and not unk else None
        else:
            st.sp = 0
        stored_slots = set()
        s0, e0 = blocks[b]
        for k in range(s0, e0):
            o, mn, ops = insns[k]
            try:
                step(st, mn, ops, emit, stored_slots, callseq)
            except Exception as ex:
                emit(mk('ERR', mn, ops), 'ERR %s %s: %r' % (mn, ops, ex))
        out[b] = st
        lm = insns[e0 - 1][1]
        if 'last' in BRINFO and (lm.startswith('b.') or lm in ('cbz', 'cbnz', 'tbz', 'tbnz')):
            binfo[b] = BRINFO['last']
    def weak(e):
        t = E.get(e, ('?',))
        k = t[0]
        if k == 'CALL':
            return 'CALL:' + str(t[1])[:40]
        if k == 'ST':
            return 'ST%d:%s' % (t[1], coarse(t[2], 1))
        if k in ('RET', 'BR'):
            return k
        if k in ('TRAP', 'BAR', 'MSR'):
            return k + str(t[1])
        if k in ('ATOM', 'STX'):
            return k + str(t[1])
        return None

    def sig(x, depth=0):
        if x is None:
            return 'NONE'
        if os.environ.get('WEAK'):
            w = sorted(set(y for y in (weak(e) for e in bev[x]) if y))
            if w:
                return '{' + ','.join(w) + '}'
            if x in binfo:
                return 'BR:' + coarse(binfo[x][0], 1)
            if depth < 6 and len(succ[x]) == 1:
                return sig(succ[x][0], depth + 1)
            return 'END:' + insns[blocks[x][1] - 1][1]
        if bev[x]:
            k = sorted(coarse(e, 3) for e in bev[x]) if os.environ.get('COARSE') else sorted(bev[x])
            return hashlib.sha1(repr(k).encode()).hexdigest()[:10]
        if x in binfo:
            return 'BR:' + (coarse(binfo[x][0], 3) if os.environ.get('COARSE') else binfo[x][0])
        if depth < 6 and len(succ[x]) == 1:
            return sig(succ[x][0], depth + 1)
        lm = insns[blocks[x][1] - 1][1]
        return 'END:' + lm
    for b, (c, taken_true) in binfo.items():
        s0, e0 = blocks[b]
        o, mn, ops = insns[e0 - 1]
        m = re.search(r'@(\d+)', ops)
        tgt = bstart.get(idx.get(int(m.group(1)), -1)) if m else None
        ft = b + 1 if b + 1 < len(blocks) else None
        st_, sf_ = (tgt, ft) if taken_true else (ft, tgt)
        key = coarse(c, 2) if os.environ.get('WEAK') else (coarse(c, 3) if os.environ.get('COARSE') else c)
        pe = 'POL|%s|T:%s|F:%s' % (key, sig(st_), sig(sf_))
        ev[pe] += 1
        desc[pe] = 'polarity ' + (pretty(c, 3)) + ' true->' + sig(st_) + ' false->' + sig(sf_)
    return ev, desc


def asum(a, b):
    ta, tb = E.get(a), E.get(b)
    if ta and ta[0] == 'SA' and cval(b) is not None:
        cb = cval(b)
        return mk('SA', ta[1] + (cb if cb < (1 << 63) else cb - (1 << 64)))
    terms, c = [], 0
    for x in (a, b):
        t = E.get(x)
        if t and t[0] == 'c':
            c += t[1]
        elif t and t[0] == 'sum':
            terms += list(t[1])
            c += t[2]
        else:
            terms.append(x)
    c &= 0xffffffffffffffff
    if not terms:
        return const(c)
    if len(terms) == 1 and c == 0:
        return terms[0]
    return mk('sum', tuple(sorted(terms)), c)


def sanon(v):
    t = E.get(v)
    return mk('SAany') if t and t[0] == 'SA' else v


def split(ops):
    rel = None
    m = re.search(r'\s*\{(.*)\}\s*$', ops)
    if m:
        rel = m.group(1).split('|')[0]
        ops = ops[:m.start()]
    return rv.split_ops(ops.strip()), rel


def step(st, mn, ops, emit, stored_slots, callseq):
    opl, rel = split(ops)

    def rd(tok):
        tok = tok.strip()
        m = REGRE.match(tok)
        if m:
            if m.group(3):
                return mk('SA', st.sp), 64
            if m.group(2) == 'zr':
                return const(0), 64 if m.group(1) == 'x' else 32
            r = 'x' + m.group(2)
            v = st.r.get(r)
            if v is None:
                v = mk('clob', 'undef')
            if m.group(1) == 'w' and W.get(v, 64) == 64:
                cv = cval(v)
                v = const(cv & 0xffffffff) if cv is not None else mk('w', v, w=32)
            return v, 32 if m.group(1) == 'w' else 64
        if tok.startswith('#'):
            return const(int(tok[1:], 0)), 64
        return mk('tok', tok), 64

    def wr(tok, v, w=None):
        m = REGRE.match(tok.strip())
        if not m:
            return
        if m.group(3):
            # sp written: only add/sub with const handled
            sa = E.get(v)
            if sa and sa[0] == 'SA':
                st.sp = sa[1]
            return
        if m.group(2) == 'zr':
            return
        r = 'x' + m.group(2)
        if m.group(1) == 'w' and W.get(v, 64) == 64:
            cv = cval(v)
            v = const(cv & 0xffffffff) if cv is not None else mk('w', v, w=32)
        st.r[r] = v
        st.fresh.add(r)

    def width(tok):
        return 32 if tok.strip().startswith('w') else 64

    def shifted(i):
        """operand i with optional shift/extend operand i+1"""
        v, w = rd(opl[i])
        if i + 1 < len(opl):
            m = re.match(r'^(lsl|lsr|asr|ror|uxtw|sxtw|uxtb|uxth|sxtb|sxth|uxtx|sxtx)\s*(#(\S+))?$', opl[i + 1].strip())
            if m:
                amt = int(m.group(3), 0) if m.group(3) else 0
                k = m.group(1)
                if k in ('uxtw', 'sxtw', 'uxtb', 'uxth', 'sxtb', 'sxth'):
                    v = mk(k, v)
                    k = 'lsl'
                if amt:
                    v = mk(k, v, const(amt))
        return v

    def addr(tok, after=None):
        """memory operand -> (address hash, writeback base reg, new base value)"""
        inner = tok.strip()
        pre = inner.endswith('!')
        inner = inner.rstrip('!').strip('[]')
        parts = [p.strip() for p in inner.split(',')]
        base, _ = rd(parts[0])
        off = None
        if len(parts) > 1:
            if parts[1].startswith('#'):
                off = const(int(parts[1][1:], 0))
            else:
                iv, _ = rd(parts[1])
                if len(parts) > 2:
                    m = re.match(r'^(lsl|uxtw|sxtw|sxtx|uxtx)\s*(#(\S+))?$', parts[2])
                    if m:
                        if m.group(1) in ('uxtw', 'sxtw'):
                            iv = mk(m.group(1), iv)
                        if m.group(3) and int(m.group(3), 0):
                            iv = mk('lsl', iv, const(int(m.group(3), 0)))
                off = iv
        if rel:
            a = mk('sym', rel)
        else:
            a = addc(base, off) if off is not None else base
        wb = None
        if pre:
            wb = (parts[0], a)
        elif after is not None:
            wb = (parts[0], addc(base, const(int(after[1:], 0))))
        return (base if (after is not None and not pre) else a), wb

    def addc(a, b):
        return asum(a, b)

    def addc_old(a, b):
        ta, tb = E.get(a), E.get(b)
        if ta and ta[0] == 'SA' and cval(b) is not None:
            return mk('SA', ta[1] + cval(b) if cval(b) < (1 << 63) else ta[1] + cval(b) - (1 << 64))
        if cval(a) is not None and cval(b) is not None:
            return const(cval(a) + cval(b))
        if ta and ta[0] == 'add' and cval(b) is not None and cval(ta[2]) is not None:
            return addc(ta[1], const(cval(ta[2]) + cval(b)))
        if cval(b) == 0:
            return a
        return op2('add', a, b, 64) if cval(b) is None else mk('add', a, b)

    def load(a, size, sgn, acq=False):
        t = E.get(a)
        if acq:
            emit(mk('ACQ', a), 'acquire-load ' + pretty(a))
        if t and t[0] == 'SA':
            v = st.slot.get(t[1])
            return v if v is not None else mk('stk?', t[1])
        return mk('ld', size, sgn, a, w=64 if size == 8 else 32)

    def store(a, v, size, rel_=False):
        t = E.get(a)
        if t and t[0] == 'SA':
            st.slot[t[1]] = v
            stored_slots.add(t[1])
            return
        v = sanon(v)
        e = mk('ST', size, a, v, rel_)
        emit(e, 'store%d %s <- %s%s' % (size, pretty(a), pretty(v), ' (release)' if rel_ else ''))

    SZ = {'b': 1, 'h': 2, 'w': 4, '': None}

    def memsize(mn, regtok):
        m = re.match(r'^(ld|st)(a?)(p?)(u?)r?(s?)([bhw]?)$', mn)
        base = mn
        for p in ('ldapur', 'ldapr', 'ldar', 'ldaxr', 'ldxr', 'stlr', 'stlxr', 'stxr', 'ldur', 'stur', 'ldr', 'str', 'ldp', 'stp', 'ldnp', 'stnp'):
            if mn.startswith(p):
                suf = mn[len(p):]
                sgn = suf.startswith('s')
                suf = suf[1:] if sgn else suf
                if suf in ('b', 'h'):
                    return (1 if suf == 'b' else 2), sgn
                if suf == 'w':
                    return 4, True
                return (4 if regtok.strip().startswith('w') else 8), sgn
        return 8, False

    if mn in ('paciasp', 'autiasp', 'nop', 'bti', 'hint', 'isb', 'clrex', 'yield', 'prfm'):
        return
    if mn in ('dmb', 'dsb'):
        emit(mk('BAR', mn, ops), mn + ' ' + ops)
        return
    if mn in ('brk', 'udf'):
        emit(mk('TRAP', ops), 'trap ' + ops)
        return
    if mn == 'mrs':
        wr(opl[0], mk('sysreg', opl[1]))
        return
    if mn == 'msr':
        emit(mk('MSR', opl[0], rd(opl[1])[0]), 'msr')
        return
    if mn in ('bl', 'blr'):
        if mn == 'bl':
            tgt = rel or ops
        else:
            tgt = 'icall:' + rd(opl[0])[0]
        args = tuple(sanon(st.r.get('x%d' % i)) if 'x%d' % i in st.fresh else None for i in range(8))
        while args and args[-1] is None:
            args = args[:-1]
        stk = tuple(sorted((s - st.sp, sanon(st.slot[s])) for s in stored_slots if 0 <= s - st.sp < 0x40))
        e = mk('CALL', tgt, args, stk)
        emit(e, 'call %s(%s)%s' % (tgt if mn == 'bl' else 'icall ' + pretty(rd(opl[0])[0], 3), ', '.join(pretty(a, 3) if a else '_' for a in args),
                                   (' stack' + str([(o, pretty(v, 2)) for o, v in stk])) if stk else ''))
        callseq[0] += 1
        rv_ = mk('ret', tgt, args[0] if args else None)
        for i in range(19):
            st.r['x%d' % i] = mk('clob', i)
            st.fresh.discard('x%d' % i)
        st.r['x30'] = mk('clob', 30)
        st.r['x0'] = rv_
        st.fresh.add('x0')
        stored_slots.clear()
        st.flags = None
        return
    if mn == 'ret':
        emit(mk('RET', st.r.get('x0')), 'ret ' + pretty(st.r.get('x0'), 3))
        return
    if mn == 'br':
        emit(mk('BR', rd(opl[0])[0]), 'br ' + pretty(rd(opl[0])[0], 3))
        return
    if mn == 'b':
        return
    if mn.startswith('b.'):
        c = cond_expr(mn[2:], st.flags)
        BRINFO['last'] = (c, not NEG[0])
        emit(mk('BRC', c), 'branch-if ' + pretty(c, 4))
        return
    if mn in ('cbz', 'cbnz'):
        v, w = rd(opl[0])
        c = cond_expr('eq', ('cmp', w, v, const(0)))
        BRINFO['last'] = (c, (not NEG[0]) == (mn == 'cbz'))
        emit(mk('BRC', c), 'branch-if ' + pretty(c, 4))
        return
    if mn in ('tbz', 'tbnz'):
        v, w = rd(opl[0])
        bit = int(opl[1].strip('#'), 0)
        t = E.get(v)
        if t and t[0] == 'sub' and ((bit == 31 and t[-1] == 'w32') or (bit == 63 and t[-1] != 'w32')):
            c = cond_expr('mi', ('cmp', 32 if bit == 31 else 64, t[1], t[2]))
            BRINFO['last'] = (c, (not NEG[0]) == (mn == 'tbnz'))
            emit(mk('BRC', c), 'branch-if ' + pretty(c, 4))
            return
        c = mk('BRT', v, bit)
        BRINFO['last'] = (c, mn == 'tbnz')
        emit(c, 'branch-if-bit%s %s' % (opl[1], pretty(v, 4)))
        return
    # memory
    if re.match(r'^(ld|st)', mn) and any(o.strip().startswith('[') for o in opl):
        mi = next(i for i, o in enumerate(opl) if o.strip().startswith('['))
        post = opl[mi + 1] if mi + 1 < len(opl) and opl[mi + 1].strip().startswith('#') else None
        regs = opl[:mi]
        isatom = rv.ATOM.match(mn) or rv.CAS.match(mn) or rv.STATOM.match(mn)
        if isatom:
            a, wb = addr(opl[mi])
            vals = [rd(r)[0] for r in regs]
            e = mk('ATOM', mn, a, tuple(vals[:1]))
            emit(e, 'atomic %s %s %s' % (mn, pretty(a), pretty(vals[0]) if vals else ''))
            if rv.ATOM.match(mn) and len(regs) > 1:
                wr(regs[1], mk('atomres', e))
            if rv.CAS.match(mn):
                wr(regs[0], mk('atomres', e))
            return
        if rv.STX.match(mn):
            a, wb = addr(opl[mi])
            e = mk('STX', mn, a, rd(regs[1])[0])
            emit(e, '%s %s <- %s' % (mn, pretty(a), pretty(rd(regs[1])[0])))
            wr(regs[0], mk('stxres', e))
            return
        size, sgn = memsize(mn, regs[0])
        a, wb = addr(opl[mi], post)
        acq = mn.startswith(('ldar', 'ldapr', 'ldaxr', 'ldapur'))
        if mn.startswith('ld'):
            for j, r in enumerate(regs):
                aa = addc(a, const(j * size)) if j else a
                wr(r, load(aa, size, sgn, acq))
        else:
            for j, r in enumerate(regs):
                aa = addc(a, const(j * size)) if j else a
                v, w = rd(r)
                store(aa, v, size, mn.startswith('stl'))
        if wb:
            wr(wb[0], wb[1])
        return
    # data processing
    d = opl[0] if opl else None
    w = width(d) if d else 64
    if mn == 'adrp':
        wr(d, mk('page', rel or opl[1]))
        return
    if mn == 'adr':
        wr(d, mk('adr', rel or 'local'))
        return
    if mn in ('add', 'adds', 'sub', 'subs') and rel:
        wr(d, mk('sym', rel))
        return
    if mn in ('cmp', 'cmn', 'tst'):
        a = rd(opl[0])[0]
        b = shifted(1)
        st.flags = ('cmp' if mn != 'tst' else 'tst', width(opl[0]), a, b) if mn != 'cmn' else ('cmp', width(opl[0]), a, mk('neg', b))
        if mn == 'tst' and a > b:
            st.flags = ('tst', width(opl[0]), b, a)
        return
    if mn in ('ccmp', 'ccmn'):
        a = rd(opl[0])[0]
        b = rd(opl[1])[0]
        if mn == 'ccmn':
            b = mk('neg', b)
        prev = st.flags
        pc = cond_expr(opl[3].strip(), prev)
        st.flags = ('ccmp', width(opl[0]), mk('ccmp', pc, opl[2].strip()), op2('pair', a, b, 64) if False else mk('pair', a, b))
        return
    if mn == 'mov' or mn == 'movz':
        if len(opl) == 2:
            v, _ = rd(opl[1])
            wr(d, v)
        return
    if mn == 'movn':
        return wr(d, mk('movn', ops))
    if mn == 'movk':
        old, _ = rd(d)
        m = re.match(r'#(-?0x[0-9a-f]+|-?\d+)(\s*,\s*lsl\s*#(\d+))?', ','.join(opl[1:]).strip())
        imm, sh = int(m.group(1), 0), int(m.group(3) or 0)
        ov = cval(old)
        if ov is None and w == 32:
            ov = 0
        if ov is not None:
            nv = (ov & ~(0xffff << sh)) | (imm << sh)
            wr(d, const(nv & (0xffffffff if w == 32 else 0xffffffffffffffff)))
        else:
            wr(d, mk('movk', old, imm, sh, w=w))
        return
    if mn in ('mvn', 'neg', 'negs'):
        v = shifted(1)
        r = mk(mn.rstrip('s') if mn != 'mvn' else 'not', v, w=w)
        if mn == 'negs':
            st.flags = ('cmp', w, const(0), v)
        wr(d, r)
        return
    if mn in ('add', 'adds', 'sub', 'subs', 'and', 'ands', 'orr', 'eor', 'bic', 'bics', 'orn', 'eon',
              'lsl', 'lsr', 'asr', 'ror', 'mul', 'sdiv', 'udiv', 'smull', 'umull', 'smulh', 'umulh', 'mneg', 'adc', 'sbc'):
        a, _ = rd(opl[1])
        b = shifted(2) if len(opl) > 2 else const(0)
        base = mn.rstrip('s') if mn in ('adds', 'subs', 'ands', 'bics') else mn
        if base == 'sub' and cval(b) is not None:
            r = addc(a, const(-cval(b))) if w == 64 or E.get(a, ('',))[0] == 'SA' else mk('sub', a, b, w=w)
        elif base == 'add' and w == 64:
            r = asum(a, b)
        elif base == 'add' and (cval(b) is not None or E.get(a, ('',))[0] == 'SA'):
            r = addc(a, b) if (E.get(a, ('',))[0] == 'SA' or w == 64) else mk('add', a, b, w=w)
        else:
            r = op2(base, a, b, w)
        if mn in ('adds', 'subs', 'ands', 'bics'):
            st.flags = ('cmp', w, a, b) if mn == 'subs' else (('cmp', w, a, mk('neg', b)) if mn == 'adds' else ('tst', w, a, b))
        if d and d.strip() in ('sp', 'wsp'):
            ta = E.get(r)
            if ta and ta[0] == 'SA':
                st.sp = ta[1]
            return
        wr(d, r)
        return
    if mn in ('madd', 'msub', 'smaddl', 'umaddl', 'smsubl', 'umsubl'):
        a, _ = rd(opl[1]); b, _ = rd(opl[2]); c, _ = rd(opl[3])
        p = op2('mul' if mn in ('madd', 'msub') else mn[:2] + 'mull', a, b, w)
        wr(d, op2('add', c, p, w) if 'add' in mn else mk('sub', c, p, w=w))
        return
    if mn in ('csel', 'csinc', 'csinv', 'csneg'):
        a, _ = rd(opl[1]); b, _ = rd(opl[2])
        cc = opl[3].strip()
        if mn == 'csinc':
            b = mk('add1', b) if cval(b) is None else const(cval(b) + 1)
        elif mn == 'csinv':
            b = mk('not', b)
        elif mn == 'csneg':
            b = mk('neg', b)
        c = cond_expr(cc, st.flags)
        # canonical: cond_expr folded negation; if it flipped, swap the arms
        ncc = NORMCC.get(cc, cc)
        fl = st.flags
        flipped = False
        if fl and fl[0] in ('cmp', 'cmn') and fl[2] > fl[3] and ncc not in ('mi', 'pl', 'vs', 'vc'):
            ncc = SWAP[ncc]
        if INV[ncc] < ncc:
            flipped = True
        if flipped:
            a, b = b, a
        wr(d, mk('csel', c, a, b, w=w))
        return
    if mn in ('cset', 'csetm', 'cinc', 'cinv', 'cneg'):
        if mn in ('cset', 'csetm'):
            a, b, cc = (const(1) if mn == 'cset' else const(-1)), const(0), opl[1].strip()
        else:
            v, _ = rd(opl[1]); cc = opl[2].strip()
            a = {'cinc': mk('add1', v), 'cinv': mk('not', v), 'cneg': mk('neg', v)}[mn]
            b = v
        c = cond_expr(cc, st.flags)
        ncc = NORMCC.get(cc, cc)
        fl = st.flags
        if fl and fl[0] in ('cmp', 'cmn') and fl[2] > fl[3] and ncc not in ('mi', 'pl', 'vs', 'vc'):
            ncc = SWAP[ncc]
        if INV[ncc] < ncc:
            a, b = b, a
        wr(d, mk('csel', c, a, b, w=w))
        return
    if mn in ('ubfx', 'sbfx', 'ubfiz', 'sbfiz', 'bfi', 'bfxil', 'extr'):
        a, _ = rd(opl[1])
        rest = tuple(x.strip() for x in opl[2:])
        if mn == 'extr':
            b, _ = rd(opl[2])
            if a == b:
                wr(d, mk('ror', a, const(int(rest[1].strip('#'), 0)), w=w))
            else:
                wr(d, mk('extr', a, b, rest[1], w=w))
            return
        if mn in ('bfi', 'bfxil'):
            old, _ = rd(d)
            wr(d, mk(mn, old, a, rest, w=w))
            return
        lsb, wid = int(rest[0].strip('#'), 0), int(rest[1].strip('#'), 0)
        if mn == 'ubfx':
            wr(d, mk('and', mk('lsr', a, const(lsb)) if lsb else a, const((1 << wid) - 1), w=w))
        elif mn == 'ubfiz':
            v = mk('and', a, const((1 << wid) - 1), w=w)
            wr(d, mk('lsl', v, const(lsb)) if lsb else v)
        else:
            wr(d, mk(mn, a, rest, w=w))
        return
    if mn in ('sxtb', 'sxth', 'sxtw', 'uxtb', 'uxth', 'uxtw', 'rev', 'rev16', 'rev32', 'clz', 'cls', 'rbit'):
        a, aw = rd(opl[1])
        if mn in ('uxtb', 'uxth'):
            wr(d, mk('and', a, const(0xff if mn == 'uxtb' else 0xffff), w=w))
        else:
            wr(d, mk(mn, a, w, w=w))
        return
    # unknown: generic
    vals = tuple(rd(x)[0] for x in opl[1:] if REGRE.match(x.strip()) or x.strip().startswith('#'))
    if d:
        wr(d, mk('op', mn, vals, w=w))
    emit(mk('UNK', mn), 'unknown-op ' + mn)


def main():
    st_, bu, sd, bd = sys.argv[1:5]
    want = sys.argv[5:]
    ea, eb = rv.Elf(st_), rv.Elf(bu)
    fa, fb = ea.functions(), eb.functions()
    da, db = rv.parse_dis(sd), rv.parse_dis(bd)
    names = want or sorted(f for f in fa if f in fb and not (fa[f][0] == fb[f][0] and fa[f][1] == fb[f][1]))
    tot = collections.Counter()
    for f in names:
        if f not in fb or f not in da or f not in db:
            print('MISSING', f)
            continue
        sa = rv.emit(da[f][1], da[f][0], len(fa[f][0]), fa[f][1])
        sb = rv.emit(db[f][1], db[f][0], len(fb[f][0]), fb[f][1])
        ea_, dsa = run_fn(sa, 's')
        eb_, dsb = run_fn(sb, 'b')
        only_s = [e for e in ea_ if e not in eb_]
        only_b = [e for e in eb_ if e not in ea_]
        cnt = [e for e in ea_ if e in eb_ and ea_[e] != eb_[e]]
        status = 'SAME' if not only_s and not only_b else 'DIFF'
        if status == 'SAME' and cnt:
            status = 'SAME-SET'
        tot[status] += 1
        print('%-8s %s  events %d/%d  only-stock %d only-built %d count-diff %d' % (status, f, sum(ea_.values()), sum(eb_.values()), len(only_s), len(only_b), len(cnt)))
        if os.environ.get('V') and status != 'SAME':
            for e in sorted(only_s, key=lambda x: dsa[x]):
                print('    - [%d] %s' % (ea_[e], dsa[e][:300]))
            for e in sorted(only_b, key=lambda x: dsb[x]):
                print('    + [%d] %s' % (eb_[e], dsb[e][:300]))
            if os.environ.get('VV'):
                for e in cnt:
                    print('    # %d/%d %s' % (ea_[e], eb_[e], dsa[e][:200]))
    print('TOTAL', dict(tot))


if __name__ == '__main__':
    main()
