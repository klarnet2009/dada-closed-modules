#!/usr/bin/env python3
"""symcmp5.py <stock.ko> <ours.ko> func... [-v]     (aarch64 .ko/.o; run on the server with `ulimit -v 16000000; nice -n 10`)
Polarity-faithful extension of symcmp4 (symcmp4.py is imported, never modified; looked up via $SYMCMP4_DIR, ../tools4, or next to this file).
What symcmp4 loses: (1) cond() facts carry only the compare CLASS, never the truth; (2) every merge of values is an unordered
phi{a|b} set, so ret(phi{0|1}) is equal whatever the conditions are; (3) a ccmp chain is an opaque key; (4) guards are only the
must-guards (intersection over occurrences).  Result: wrong/missing range-check variants, swapped branch sense etc. gave
CLASS-A-auto / COND-FORM.
symcmp5 therefore: (a) decodes every compare (cmp/cmn/tst/cbz/tbz/b.cc/csel/cset/ccmp/ccmn, constant folding, bool materialisation)
into an exact boolean formula over canonical atoms (operands order-normalised, signedness kept: lt/ge vs lo/hs are different atoms);
(b) computes for every block its exact reach condition as a BDD (back edges of loops ignored); (c) merges values GATED: a value at a
join is G{leaf@reach|...} (reach = exact path condition, canonical: independent of block order, branch sense, ccmp-vs-branch chains);
(d) compares per fact (call/st/stkarg/atomic/msr/tailcall/trap/barrier/insn/ret leaf) the OR of its reach conditions between the two
builds (modulo mutual exclusion of lt/gt/eq and lo/hi/eq of the same operand pair).
Verdicts per function: POLARITY-DIFF (same fact/value, different path condition or different gating; both sides printed as
cubes of compare forms), FACT-DIFF (fact only on one side), CLEAN-POL (none).  Loops: the reach of a loop body ignores iteration
(approximation, printed as loops n).  Atom operands are phi/out/in_x-normalised (as symcmp4 nphi), so two atoms differing only
inside a phi operand are merged."""
import os, re, sys, collections, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (os.environ.get('SYMCMP4_DIR'), os.path.join(HERE, '..', 'tools4'), HERE):
    if _p and os.path.exists(os.path.join(_p, 'symcmp4.py')):
        sys.path.insert(0, _p)
        break
import symcmp4 as S4
from symcmp4 import H, unH, full, isint, Elf

GM = {}   # gated value (hash) -> [(leaf, bdd node)]


class Cap(Exception):
    pass


class BDD:
    def __init__(s, order=None):
        s.order = dict(order) if order is not None else {}
        s.dyn = order is None
        s.nodes = [(1 << 30, 0, 0), (1 << 30, 1, 1)]
        s.uniq, s.memo = {}, {}

    def mk(s, v, lo, hi):
        if lo == hi:
            return lo
        k = (v, lo, hi)
        r = s.uniq.get(k)
        if r is None:
            if len(s.nodes) > int(os.environ.get('S5CAP', '700000')):
                raise Cap()
            r = len(s.nodes)
            s.nodes.append(k)
            s.uniq[k] = r
        return r

    def var(s, name):
        i = s.order.get(name)
        if i is None:
            if not s.dyn:
                raise KeyError(name)
            i = len(s.order)
            s.order[name] = i
        return s.mk(i, 0, 1)

    def ap(s, op, a, b):
        if op:    # and
            if a == 0 or b == 0: return 0
            if a == 1: return b
            if b == 1: return a
        else:
            if a == 1 or b == 1: return 1
            if a == 0: return b
            if b == 0: return a
        if a == b:
            return a
        if a > b:
            a, b = b, a
        k = (op, a, b)
        r = s.memo.get(k)
        if r is not None:
            return r
        va, la, ha = s.nodes[a]
        vb, lb, hb = s.nodes[b]
        v = min(va, vb)
        r = s.mk(v, s.ap(op, la if va == v else a, lb if vb == v else b), s.ap(op, ha if va == v else a, hb if vb == v else b))
        s.memo[k] = r
        return r

    def AND(s, a, b):
        return s.ap(1, a, b)

    def OR(s, a, b):
        return s.ap(0, a, b)

    def NOT(s, a):
        if a < 2:
            return 1 - a
        k = ('n', a)
        r = s.memo.get(k)
        if r is None:
            v, lo, hi = s.nodes[a]
            r = s.mk(v, s.NOT(lo), s.NOT(hi))
            s.memo[k] = r
        return r

    def cubes(s, f, limit=6):
        names = {i: n for n, i in s.order.items()}
        out = []

        def go(n, cur):
            if len(out) >= limit or n == 0:
                return
            if n == 1:
                out.append(list(cur))
                return
            v, lo, hi = s.nodes[n]
            go(lo, cur + [(names[v], False)])
            go(hi, cur + [(names[v], True)])
        go(f, [])
        return out


def build_care(bm, atoms):
    grp = collections.defaultdict(dict)
    for nm in atoms:
        m = re.match(r'^(cmp[wx]\(.*\)):(lt|gt|eq|lo|hi)$', nm)
        if m:
            grp[m.group(1)][m.group(2)] = nm
    c = 1
    for base, d in grp.items():
        for cls in (('lt', 'gt', 'eq'), ('lo', 'hi', 'eq')):
            have = [bm.var(d[x]) for x in cls if x in d]
            for i in range(len(have)):
                for j in range(i + 1, len(have)):
                    c = bm.AND(c, bm.NOT(bm.AND(have[i], have[j])))
            if len(have) == 3:
                c = bm.AND(c, bm.OR(have[0], bm.OR(have[1], have[2])))
    # same operand vs different constants: eq/lt(signed)/lo(unsigned) facts imply or exclude each other
    cg = collections.defaultdict(list)
    for nm in atoms:
        m = re.match(r'^cmp([wx])\((.*),(-?\d+)\):(eq|lt|lo)$', nm)
        if m:
            cg[(m.group(1), m.group(2))].append((m.group(4), int(m.group(3)), nm))
    for (w, X), L in cg.items():
        bits = 32 if w == 'w' else 64
        sg = lambda v: v - (1 << bits) if (v & ((1 << bits) - 1)) >> (bits - 1) else v & ((1 << bits) - 1)
        un = lambda v: v & ((1 << bits) - 1)
        for i in range(len(L)):
            for j in range(len(L)):
                if i == j:
                    continue
                (k1, c1, n1), (k2, c2, n2) = L[i], L[j]
                a, b = bm.var(n1), bm.var(n2)
                imp = lambda x, y: bm.OR(bm.NOT(x), y)       # x => y
                if k1 == 'eq' and k2 == 'eq' and i < j and un(c1) != un(c2):
                    c = bm.AND(c, bm.NOT(bm.AND(a, b)))
                elif k1 == 'eq' and k2 == 'lt':
                    c = bm.AND(c, imp(a, b) if sg(c1) < sg(c2) else imp(a, bm.NOT(b)))
                elif k1 == 'eq' and k2 == 'lo':
                    c = bm.AND(c, imp(a, b) if un(c1) < un(c2) else imp(a, bm.NOT(b)))
                elif k1 == 'lt' and k2 == 'lt' and sg(c1) < sg(c2):
                    c = bm.AND(c, imp(a, b))
                elif k1 == 'lo' and k2 == 'lo' and un(c1) < un(c2):
                    c = bm.AND(c, imp(a, b))
    return c


def opq(x):
    h = 'H' + hashlib.md5(x.encode()).hexdigest()[:8]
    S4.HMAP[h] = x
    return h


def nph(v):
    v = full(v)
    prev = None
    while prev != v:
        prev = v
        v = re.sub(r'(?:phi|G)\{[^{}]*\}', 'PHI', v)
    v = re.sub(r'out\([^()]*\)', 'OUT', v)
    v = re.sub(r'ret1\(', 'RET1(', v)
    return re.sub(r'@#\d+#', '', re.sub(r'in_x\d+', 'IN', v))


def ccval(cc, N, Z, C, V):
    cc = S4.ALIAS.get(cc, cc)
    t = {'eq': Z, 'ne': not Z, 'hs': C, 'lo': not C, 'mi': N, 'pl': not N, 'vs': V, 'vc': not V, 'hi': C and not Z,
         'ls': not (C and not Z), 'ge': N == V, 'lt': N != V, 'gt': (not Z) and N == V, 'le': Z or N != V, 'al': True, 'nv': True}
    return 1 if t[cc] else 0


def fold(kind, w, a, b, cc):
    bits = 32 if w == 'w' else 64
    mask = (1 << bits) - 1
    ua, ub = int(a) & mask, int(b) & mask
    sg = lambda u: u - (1 << bits) if u >> (bits - 1) else u
    if kind == 'cmp':
        r = (ua - ub) & mask
        C = ua >= ub
        d = sg(ua) - sg(ub)
        V = not (-(1 << (bits - 1)) <= d < (1 << (bits - 1)))
    elif kind == 'cmn':
        r = (ua + ub) & mask
        C = ua + ub > mask
        d = sg(ua) + sg(ub)
        V = not (-(1 << (bits - 1)) <= d < (1 << (bits - 1)))
    else:
        r, C, V = ua & ub, False, False
    return ccval(cc, bool(r >> (bits - 1)), r == 0, C, V)


FLSET = ('cmp', 'cmn', 'tst', 'ccmp', 'ccmn', 'subs', 'adds', 'ands', 'bics')
CSEL = ('csel', 'csinc', 'csinv', 'csneg', 'cset', 'csetm', 'cinc', 'cinv', 'cneg')
FOLDOPS = ('and', 'orr', 'eor', 'add', 'sub', 'bic', 'orn', 'eon')
CBR = ('cbz', 'cbnz', 'tbz', 'tbnz')
IDXS = {'cmp': (2, 3), 'cmn': (2, 3), 'tst': (2, 3), 'bit': (1,)}


class PFn(S4.Fn):
    def __init__(self, rows, arity, bm):
        super().__init__(rows, arity)
        self.bm, self.atoms, self.R, self.bc, self.adisp, self.cur = bm, set(), {}, {}, {}, None
        self.nloops = 0

    # ---- gated values
    def comps(self, v, g):
        c = GM.get(v)
        if c is None:
            return [(v, g)]
        return [(l, self.bm.AND(x, g)) for l, x in c]

    def mkval(self, parts):
        d = {}
        for l, g in parts:
            if g:
                d[l] = self.bm.OR(d[l], g) if l in d else g
        if not d:
            return 'U'
        if len(d) == 1:
            return next(iter(d))
        items = sorted(d.items())
        gs = 'G{' + '|'.join('%s@#%d#' % (l, g) for l, g in items) + '}'
        h = opq(gs)     # always opaque: symcmp4's phi merge splits raw strings at '|'
        GM[h] = items
        return h

    def gv(self, pairs):
        if all(v == pairs[0][0] for v, E in pairs) and pairs[0][0] not in GM:
            return pairs[0][0]
        parts = []
        for v, E in pairs:
            parts += self.comps(v, E)
        return self.mkval(parts)

    def gmerge(self, items, hdr):
        if len(items) == 1:
            return items[0][0].copy()
        if hdr:     # loop header: values that differ between the entry and the back edge collapse to PHI (as symcmp4 nphi)
            n = S4.St()
            for k in set().union(*[s.r.keys() for s, E in items]):
                vs = [s.r.get(k, 'U') for s, E in items]
                n.r[k] = vs[0] if all(v == vs[0] for v in vs) else 'PHI'
            for k in set().union(*[s.stk.keys() for s, E in items]):
                vs = [s.stk.get(k, 'U') for s, E in items]
                n.stk[k] = vs[0] if all(v == vs[0] for v in vs) else 'PHI'
            fls = [s.fl for s, E in items]
            n.fl = fls[0] if all(f == fls[0] for f in fls) else ('phiflags',)
            n.esc = set().union(*[s.esc for s, E in items])
            lc = [s.lastcall for s, E in items]
            n.lastcall = lc[0] if all(x == lc[0] for x in lc) else 'phi'
            return n
        n = S4.St()
        for k in set().union(*[s.r.keys() for s, E in items]):
            n.r[k] = self.gv([(s.r.get(k, 'U'), E) for s, E in items])
        for k in set().union(*[s.stk.keys() for s, E in items]):
            n.stk[k] = self.gv([(s.stk.get(k, 'U'), E) for s, E in items])
        fls = [s.fl for s, E in items]
        n.fl = fls[0] if all(f == fls[0] for f in fls) else ('phiflags',)
        n.esc = set().union(*[s.esc for s, E in items])
        lc = [s.lastcall for s, E in items]
        n.lastcall = lc[0] if all(x == lc[0] for x in lc) else 'phi'
        return n

    # ---- conditions
    def fltext(self):
        if not self.cur:
            return '?'
        i = self.idx[self.cur[0]]
        for j in range(i, max(i - 25, -1), -1):
            a, m, ops, rel = self.rows[j]
            if m in FLSET or m in CBR:
                return '%s %s' % (m, ', '.join(ops))
        return '?'

    def atom(self, name):
        self.atoms.add(name)
        if self.cur and name not in self.adisp:
            a, m, ops, rel = self.cur
            self.adisp[name] = '%s ; %s %s @%x' % (self.fltext(), m, ', '.join(ops), a)
        return self.bm.var(name)

    def evalcond(self, fl, cc):
        bm = self.bm
        cc = S4.ALIAS.get(cc, cc)
        if fl is None:
            return self.atom('?flags@%s:%s' % (self.cur[0] if self.cur else '', cc))
        if fl[0] == 'ccmp2':
            _, prev, ccond, w, x, y, nz, isn = fl
            tc = self.evalcond(prev, ccond)
            if isn and isint(y):
                inner = ('cmp', w, x, str(-int(y)))
            else:
                inner = ('cmn' if isn else 'cmp', w, x, y)
            et = self.evalcond(inner, cc)
            ef = ccval(cc, nz >> 3 & 1, nz >> 2 & 1, nz >> 1 & 1, nz & 1)
            return bm.OR(bm.AND(tc, et), bm.AND(bm.NOT(tc), ef))
        kind = fl[0]
        for i in IDXS.get(kind, ()):
            if fl[i] in GM:
                res = 0
                for leaf, g in GM[fl[i]]:
                    res = bm.OR(res, bm.AND(g, self.evalcond(fl[:i] + (leaf,) + fl[i + 1:], cc)))
                return res
        if kind == 'cmp' and 'SP_EL0)+1568' in full(fl[2]) + full(fl[3]):     # stack-protector canary check: assumed equal
            return fold('cmp', fl[1], '0', '0', cc)
        if kind in ('cmp', 'cmn', 'tst') and isint(fl[2]) and isint(fl[3]):
            return fold(kind, fl[1], fl[2], fl[3], cc)
        if kind == 'bit' and isint(fl[1]):
            c = (int(fl[1]) >> int(fl[2])) & 1
            return 1 if (c == 0) == (cc == 'z') else 0
        nf = tuple(nph(e) if isinstance(e, str) else e for e in fl)
        k, t = S4.lit(nf, cc)
        v = self.atom(unH(k))
        return v if t else bm.NOT(v)

    def branch_cond(self, st, m, ops):
        if m.startswith('b.'):
            return self.evalcond(st.fl, m[2:])
        x = self.val(st, ops[0])
        w = self.w(ops[0])
        if m in ('cbz', 'cbnz'):
            return self.evalcond(('cmp', w, x, '0'), 'eq' if m == 'cbz' else 'ne')
        bit = int(ops[1][1:], 0)
        if (w == 'w' and bit == 31) or (w == 'x' and bit == 63):
            return self.evalcond(('cmp', w, x, '0'), 'ge' if m == 'tbz' else 'lt')
        return self.evalcond(('bit', x, str(bit)), 'z' if m == 'tbz' else 'nz')

    def step(self, row, st, facts, b):
        a, m, ops, rel = row
        self.cur = row
        bm = self.bm
        if m in CBR or m.startswith('b.'):
            self.bc[b] = self.branch_cond(st, m, ops)
            return
        if m == 'b' and rel:      # symcmp4 emits tailcall(fn) without arguments: add x0..x7 (value, or '-' when untouched)
            if facts is not None:
                args = [S4.stkn(st.r.get(k, 'U')) if st.r.get(k, 'U') != 'a%d' % k else '-' for k in range(8)]
                self.emit(facts, b, a, 'tailcall(%s;%s)' % (rel, ';'.join(args)))
            return
        if m == 'ret':
            if facts is not None:
                for leaf, g in self.comps(st.r.get(0, 'U'), self.R[b]):
                    facts.append((H('ret(%s)' % S4.stkn(leaf)), b, a, g))
            return
        if m in ('ccmp', 'ccmn'):
            st.fl = ('ccmp2', st.fl, ops[3], self.w(ops[0]), self.val(st, ops[0]), self.val(st, ops[1]), int(ops[2][1:], 0), m == 'ccmn')
            return
        if m in CSEL and len(ops) >= 2:
            f = self.evalcond(st.fl, ops[-1])
            if m in ('cset', 'csetm'):
                tv, fv = ('1', '0') if m == 'cset' else ('-1', '0')
            elif m in ('cinc', 'cinv', 'cneg'):
                x = self.val(st, ops[1])
                tv, fv = H('%s(%s)' % (m, x)), x
                if isint(x):
                    mk = (1 << (32 if self.w(ops[0]) == 'w' else 64)) - 1
                    tv = str({'cinc': int(x) + 1, 'cinv': ~int(x), 'cneg': -int(x)}[m] & mk)
            else:
                x, y = self.val(st, ops[1]), self.val(st, ops[2])
                tv, fv = x, (y if m == 'csel' else H('%s(%s)' % (m, y)))
                if m != 'csel' and isint(y):
                    mk = (1 << (32 if self.w(ops[0]) == 'w' else 64)) - 1
                    fv = str({'csinc': int(y) + 1, 'csinv': ~int(y), 'csneg': -int(y)}[m] & mk)
            Rb = self.R[b]
            self.setr(st, ops[0], self.mkval(self.comps(tv, bm.AND(Rb, f)) + self.comps(fv, bm.AND(Rb, bm.NOT(f)))))
            return
        if m in FOLDOPS and len(ops) == 3 and S4.REGRE.match(ops[0]):      # boolean algebra on materialised flags: exact fold per leaf
            s1, s2 = self.val(st, ops[1]), self.val(st, ops[2])
            if (s1 in GM or s2 in GM) and all(isint(s) or (s in GM and all(isint(l) for l, _ in GM[s])) for s in (s1, s2)):
                mk = (1 << (32 if self.w(ops[0]) == 'w' else 64)) - 1
                parts = []
                for l1, g1 in self.comps(s1, 1):
                    for l2, g2 in self.comps(s2, 1):
                        x, y = int(l1), int(l2)
                        r = {'and': x & y, 'orr': x | y, 'eor': x ^ y, 'add': x + y, 'sub': x - y, 'bic': x & ~y, 'orn': x | ~y, 'eon': x ^ ~y}[m]
                        parts.append((str(r & mk), bm.AND(g1, g2)))
                self.setr(st, ops[0], self.mkval(parts))
                return
        super().step(row, st, facts, b)
        if m in ('ldp', 'ldnp', 'ldpsw'):     # pair loads are named ldp.* by symcmp4: same value as the single ldr.* of the other build
            for rg in ops[:2]:
                mm = S4.REGRE.match(rg)
                if mm and mm.group(2) != 'zr' and int(mm.group(2)) in st.r:
                    u = unH(st.r[int(mm.group(2))])
                    if u.startswith(('ldp.', 'ldnp.', 'ldpsw.')):
                        st.r[int(mm.group(2))] = H(re.sub(r'^ld(?:n?p)sw?\.', lambda x: 'ldrsw.' if 'sw' in x.group(0) else 'ldr.', u))

    def edge(self, p, k):
        m = self.rows[self.blocks[p][1] - 1][1]
        if (m in CBR or m.startswith('b.')) and p in self.bc:
            if k == 'T':
                return self.bc[p]
            if k == 'F':
                return self.bm.NOT(self.bc[p])
        return 1

    def analyse2(self):
        bm, nb = self.bm, len(self.blocks)
        color, post, back = {0: 1}, [], set()
        stack = [(0, iter(self.succ[0]))]
        while stack:
            b, it = stack[-1]
            for t, k in it:
                if t is None:
                    continue
                if color.get(t) == 1:
                    back.add((b, t))
                elif t not in color:
                    color[t] = 1
                    stack.append((t, iter(self.succ[t])))
                    break
            else:
                color[b] = 2
                post.append(b)
                stack.pop()
        order = post[::-1]
        hdr = {t for b, t in back}
        self.nloops = len(hdr)
        e0 = S4.St()
        for k in range(8):
            e0.r[k] = 'a%d' % k
        for k in range(19, 30):
            e0.r[k] = 'in_x%d' % k
        e0.r['sp'] = 'SP+0'
        ins, outs, lastR = [None] * nb, [None] * nb, {}
        for it in range(60):
            changed = False
            for b in order:
                if b == 0:
                    sin, self.R[0] = e0.copy(), 1
                else:
                    allp, Rb = [], 0
                    for p, k in self.pred[b]:
                        if outs[p] is None:
                            continue
                        E = bm.AND(self.R[p], self.edge(p, k))
                        allp.append((outs[p], E))
                        if (p, b) not in back:
                            Rb = bm.OR(Rb, E)
                    if not allp:
                        continue
                    self.R[b] = Rb
                    sin = self.gmerge(allp, b in hdr)
                if ins[b] is not None and ins[b].key() == sin.key() and lastR.get(b) == self.R[b]:
                    continue
                ins[b], lastR[b] = sin, self.R[b]
                outs[b] = self.run(b, sin.copy())
                changed = True
            if not changed:
                break
        facts = []
        for b in range(nb):
            if ins[b] is None:
                continue
            st = ins[b].copy()
            s, e = self.blocks[b]
            for i in range(s, e):
                self.step(self.rows[i], st, facts, b)
        return facts


def summ(fn, facts):
    d = {}
    for t in facts:
        f, b, a = t[:3]
        if unH(f).startswith('cond('):
            continue
        g = t[3] if len(t) > 3 else fn.R.get(b, 0)
        c, r, ad = d.get(f, (0, 0, []))
        d[f] = (c + 1, fn.bm.OR(r, g), ad + [a])
    return d


def support(bm, f, acc=None):
    acc = set() if acc is None else acc
    seen, st = set(), [f]
    names = {i: n for n, i in bm.order.items()}
    while st:
        n = st.pop()
        if n < 2 or n in seen:
            continue
        seen.add(n)
        v, lo, hi = bm.nodes[n]
        acc.add(names[v])
        st += [lo, hi]
    return acc


def gates_equal(bm, care, fs_, fo_, rs_, ro_):
    """gated values of two facts with the same shape: every leaf has the same gate, restricted to where the fact itself is reached, modulo the care set"""
    ts = [t for t in re.findall(r'H[0-9a-f]{8}', S4.HMAP.get(fs_, fs_)) if t in GM]
    to = [t for t in re.findall(r'H[0-9a-f]{8}', S4.HMAP.get(fo_, fo_)) if t in GM]
    if not ts or len(ts) != len(to):
        return False
    for a, b in zip(ts, to):
        da, db = dict(GM[a]), dict(GM[b])
        if set(da) != set(db):
            return False
        for l in da:
            if bm.AND(bm.OR(bm.AND(bm.AND(da[l], rs_), bm.NOT(bm.AND(db[l], ro_))), bm.AND(bm.AND(db[l], ro_), bm.NOT(bm.AND(da[l], rs_)))), care):
                return False
    return True



def flat_expand(bm, v, g, cap=3000):
    """expand a (possibly nested) gated value string fully: -> {normalised leaf string: region bdd}"""
    out = {}
    work = [(v, g)]
    n = 0
    while work:
        s, r = work.pop()
        if not r:
            continue
        m = re.search(r'H[0-9a-f]{8}', s)
        if not m:
            k = re.sub(r'(?:phi)\{[^{}]*\}', 'PHI', s)
            k = re.sub(r'out\([^()]*\)', 'OUT', k)
            k = re.sub(r'ret1\(', 'RET1(', k)
            k = re.sub(r'@#\d+#', '', re.sub(r'in_x\d+', 'IN', k))
            out[k] = bm.OR(out[k], r) if k in out else r
            continue
        n += 1
        if n > cap:
            raise Cap()
        t = m.group(0)
        if t in GM:
            for leaf, gg in GM[t]:
                work.append((s[:m.start()] + leaf + s[m.end():], bm.AND(r, gg)))
        else:
            work.append((s[:m.start()] + S4.HMAP.get(t, 'UNK' + t[1:]) + s[m.end():], r))
    return out


def flat_diff(bm, care, fs_, fo_, rs_, ro_):
    a = flat_expand(bm, fs_, rs_)
    b = flat_expand(bm, fo_, ro_)
    res = []
    for k in set(a) | set(b):
        d1 = bm.AND(bm.AND(a.get(k, 0), bm.NOT(b.get(k, 0))), care)
        d2 = bm.AND(bm.AND(b.get(k, 0), bm.NOT(a.get(k, 0))), care)
        if d1 or d2:
            res.append((k, d1, d2))
    return res


def shape(f):
    return re.sub(r'@#\d+#', '@', full(f))


def fmtf(f, n=170):
    return re.sub(r'H[0-9a-f]{8}', lambda m: '<' + S4.HMAP.get(m.group(0), m.group(0))[:90] + '>', f)[:n]


def run_pair(sk, ok, fname, es, eo, verbose):
    rs, ro = S4.listing(sk, fname, es), S4.listing(ok, fname, eo)
    ar = {}
    S4.arity_pass(rs, ar)
    S4.arity_pass(ro, ar)
    atoms = set()
    for rows in (rs, ro):
        GM.clear()
        f = PFn(rows, ar, BDD())
        f.analyse2()
        atoms |= f.atoms
    for attempt in range(40):
        try:
            bm = BDD({n: i for i, n in enumerate(sorted(atoms))})
            care = build_care(bm, atoms)
            GM.clear()
            P = []
            for rows in (rs, ro):
                f = PFn(rows, ar, bm)
                P.append((f, summ(f, f.analyse2())))
            break
        except KeyError as e:
            if os.environ.get('S5DBG'):
                print('missing atom', e.args[0][:200])
            atoms.add(e.args[0])
    (fs, S), (fo, O) = P
    al = {}
    A = lambda n: al.setdefault(n, 'A%d' % (len(al) + 1))
    cubes = lambda g: [' & '.join('%s%s' % ('' if p else '!', A(n)) for n, p in c) or 'TRUE' for c in bm.cubes(g, 4)]
    pol = []
    for f in set(S) & set(O):
        d1 = bm.AND(bm.AND(S[f][1], bm.NOT(O[f][1])), care)
        d2 = bm.AND(bm.AND(O[f][1], bm.NOT(S[f][1])), care)
        if d1 or d2:
            pol.append((f, d1, d2, 'reach'))
    so = {f for f in S if f not in O}
    oo = {f for f in O if f not in S}
    sh_o = collections.defaultdict(list)
    for f in oo:
        sh_o[shape(f)].append(f)
    only_s, paired = [], set()
    for f in sorted(so, key=lambda x: S[x][2][0]):
        c = [g for g in sh_o.get(shape(f), []) if g not in paired]
        if c:
            paired.add(c[0])
            d1 = bm.AND(bm.AND(S[f][1], bm.NOT(O[c[0]][1])), care)
            d2 = bm.AND(bm.AND(O[c[0]][1], bm.NOT(S[f][1])), care)
            if not d1 and not d2 and gates_equal(bm, care, f, c[0], S[f][1], O[c[0]][1]):
                continue
            if not d1 and not d2:
                try:
                    fd = flat_diff(bm, care, f, c[0], S[f][1], O[c[0]][1])
                except Cap:
                    fd = None
                if fd == []:
                    continue
                pol.append((f, d1, d2, 'gating', c[0], fd))
                continue
            pol.append((f, d1, d2, 'gating', c[0], None))
        else:
            only_s.append(f)
    only_o = [f for f in sorted(oo, key=lambda x: O[x][2][0]) if f not in paired]
    cnt = [f for f in set(S) & set(O) if S[f][0] != O[f][0]]
    common = fs.atoms & fo.atoms
    pol_true = [t for t in pol if support(bm, t[2], support(bm, t[1])) <= common]
    pol_atom = [t for t in pol if t not in pol_true]
    verdict = ('POLARITY-DIFF' if pol_true else 'POLARITY-DIFF-ATOMS') if pol else ('FACT-DIFF' if (only_s or only_o) else 'CLEAN-POL')
    print('=== %s: stock %d insns/%d blocks, ours %d/%d; facts %d/%d atoms %d loops %d/%d' % (
        fname, len(rs), len(fs.blocks), len(ro), len(fo.blocks), len(S), len(O), len(atoms), fs.nloops, fo.nloops))
    print('  SUMMARY5 %s: polarity-diff %d (same-atoms %d, one-sided-atoms %d) | only-stock %d only-ours %d | count-diff %d => %s' % (
        fname, len(pol), len(pol_true), len(pol_atom), len(only_s), len(only_o), len(cnt), verdict))
    if os.environ.get('S5ATOMS'):
        for n in sorted(fs.atoms - fo.atoms):
            print('  S-only atom %s  [%s]' % (n[:140], fs.adisp.get(n, '-')[:80]))
        for n in sorted(fo.atoms - fs.atoms):
            print('  O-only atom %s  [%s]' % (n[:140], fo.adisp.get(n, '-')[:80]))
        for f in only_s[:6]:
            print('  - S@%x %s' % (S[f][2][0], fmtf(f, 150)))
        for f in only_o[:6]:
            print('  + O@%x %s' % (O[f][2][0], fmtf(f, 150)))
        return verdict
    for t in (pol_true + pol_atom)[:12]:
        f, d1, d2, kind = t[:4]
        print('  %s [%s] %s' % ('POLARITY-DIFF' if t in pol_true else 'POLARITY-DIFF-ATOMS', kind, fmtf(f)))
        if kind == 'gating':
            print('      ours-variant: %s' % fmtf(t[4]))
            fd = t[5] if len(t) > 5 else None
            if fd is None:
                print('      (flat expansion not available)')
            for k, e1, e2 in (fd or [])[:6]:
                print('      FLATLEAF %s' % fmtf(k, 200))
                print('        only-stock: ' + ' || '.join(cubes(e1)[:4]))
                print('        only-ours : ' + ' || '.join(cubes(e2)[:4]))
        if os.environ.get('S5SUP'):
            sup = sorted(support(bm, bm.OR(d1, d2)))
            print('      xor-support: ' + ' '.join(A(n) + '[' + fs.adisp.get(n, fo.adisp.get(n, '-'))[:0] + ']' for n in sup))
            for n in sup:
                print('        %s = %s' % (A(n), n[:110]))
            continue
        if os.environ.get('S5BOTH') and kind == 'reach':
            for tag, g in (('S-reach', S[f][1]), ('O-reach', O[f][1])):
                print('      %s: ' % tag + ' || '.join(c for c in cubes(bm.AND(g, care))[:int(os.environ.get('S5BOTH'))]))
        if os.environ.get('S5BRIEF'):
            print('      only-stock: ' + ' || '.join(c[:int(os.environ.get('S5BRIEF'))] for c in cubes(d1)[:int(os.environ.get('S5NC', '2'))]))
            print('      only-ours : ' + ' || '.join(c[:int(os.environ.get('S5BRIEF'))] for c in cubes(d2)[:int(os.environ.get('S5NC', '2'))]))
            continue
        print('      region reached only in STOCK (as compare forms):')
        for c in cubes(d1):
            print('        ' + c)
        print('      region reached only in OURS:')
        for c in cubes(d2):
            print('        ' + c)
    for f in only_s[:15]:
        print('  - S@%x %s' % (S[f][2][0], fmtf(f)))
    for f in only_o[:15]:
        print('  + O@%x %s' % (O[f][2][0], fmtf(f)))
    if al:
        print('  atoms used above (A<n> = canonical compare, then the nearest flag-setting asm of each build):')
        sel = os.environ.get('S5SEL')
        for n, a_ in sorted(al.items(), key=lambda kv: int(kv[1][1:])):
            if sel is not None and a_[1:] not in sel.split(','):
                continue
            print('    %s = %s   [S: %s | O: %s]' % (a_, n[:150], fs.adisp.get(n, '-')[:70], fo.adisp.get(n, '-')[:70]))
    if verbose:
        for f in cnt[:20]:
            print('  # count S%d O%d %s' % (S[f][0], O[f][0], fmtf(f, 140)))
    return verdict


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('-')]
    verbose = '-v' in sys.argv
    sk, ok = args[0], args[1]
    es, eo = Elf(sk), Elf(ok)
    names = args[2:]
    if names == ['all']:     # every function present in both builds whose instruction text differs
        import signal
        both = sorted({x.name for x in es.syms if x.type == 2} & {x.name for x in eo.syms if x.type == 2})
        names, same = [], 0
        for n in both:
            try:
                a = [(m, tuple(o), r) for _, m, o, r in S4.listing(sk, n, es)]
                b = [(m, tuple(o), r) for _, m, o, r in S4.listing(ok, n, eo)]
            except Exception:
                a, b = 0, 1
            if a == b:
                same += 1
            else:
                names.append(n)
        print('# all: %d common functions, %d identical text, %d to compare' % (len(both), same, len(names)))
        signal.signal(signal.SIGALRM, lambda *x: (_ for _ in ()).throw(Cap()))
    for fname in names:
        if args[2:] == ['all']:
            signal.alarm(int(os.environ.get('S5TMO', '240')))
        try:
            run_pair(sk, ok, fname, es, eo, verbose)
        except Cap:
            print('=== %s: SUMMARY5 INCONCLUSIVE (BDD node cap or timeout)' % fname)
        except Exception as ex:
            import traceback
            traceback.print_exc()
            print('=== %s: SUMMARY5 ERROR %r' % (fname, ex))


if __name__ == '__main__':
    main()
