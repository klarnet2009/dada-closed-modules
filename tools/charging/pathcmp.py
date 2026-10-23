#!/usr/bin/env python3
"""pathcmp.py <stock.ko> <ours.ko> func... [-v]   (G22c; builds on symcmp4)
Path pairing: for every emitted effect fact the FULL reachability condition (OR over all acyclic paths of AND of branch literals,
polarity kept, canonical compare atoms from symcmp4.lit) as a ROBDD over a shared unique table; the formulas of both builds are
compared by node id. Back edges (loops) are ignored; jump-table edges ('J') add no literal (weak: equal-structure assumption).
LIMIT: phi-merges of VALUES do not track the incoming edge condition (a fact argument phi{a|b} is compared as the set {a,b})."""
import sys, re, collections
sys.path.insert(0, '<work-dir>')
import symcmp4 as S4
from symcmp4 import Elf, listing, arity_pass, Fn, summarize, fmt, unH, nphi

UT = {}; NODES = [None, None]
VAR = {}; VARN = []
def var(name):
    if name not in VAR:
        VAR[name] = len(VARN); VARN.append(name)
    return VAR[name]
def mk(v, lo, hi):
    if lo == hi: return lo
    k = (v, lo, hi)
    if k not in UT:
        UT[k] = len(NODES); NODES.append(k)
    return UT[k]
MEMO = {}
def ite(f, g, h):
    if f == 1: return g
    if f == 0: return h
    if g == h: return g
    if g == 1 and h == 0: return f
    k = (f, g, h)
    if k in MEMO: return MEMO[k]
    v = min(NODES[x][0] for x in (f, g, h) if x > 1)
    def cof(x, b):
        if x <= 1 or NODES[x][0] != v: return x
        return NODES[x][2] if b else NODES[x][1]
    r = mk(v, ite(cof(f, 0), cof(g, 0), cof(h, 0)), ite(cof(f, 1), cof(g, 1), cof(h, 1)))
    MEMO[k] = r; return r
def AND(a, b): return ite(a, b, 0)
def OR(a, b): return ite(a, 1, b)
def NOT(a): return ite(a, 0, 1)
def atom(key, truth):
    n = mk(var(key), 0, 1)
    return n if truth else NOT(n)
def cubes(f, lim=6, pre=()):
    if f == 0: return []
    if f == 1: return [pre]
    v, lo, hi = NODES[f]
    out = cubes(hi, lim, pre + ((v, 1),))
    if len(out) < lim: out += cubes(lo, lim - len(out), pre + ((v, 0),))
    return out[:lim]
def fmtcube(c):
    return ' & '.join(('' if t else '!') + fmt(VARN[v])[:90] for v, t in c) or 'TRUE'
def short(f):
    cs = cubes(f, 3)
    return ' | '.join(fmtcube(c) for c in cs) if cs else 'FALSE'

# ---------------- path-conditioned phi (G22c): merge values carry the absolute reach formula (BDD node id) of each incoming value
from symcmp4 import St, H as H4, merge as merge4
import symcmp4 as _S
def nphi2(v):
    """like nphi but only UNTAGGED phi (loop headers) collapse; tagged phi{v@id|...} keep their conditions"""
    v = _S.full(v)
    prev = None
    while prev != v:
        prev = v
        v = re.sub(r'phi\{[^{}@]*\}', 'PHI', v)
    v = re.sub(r'out\([^()]*\)', 'OUT', v)
    v = re.sub(r'ret1\(', 'RET1(', v)
    v = re.sub(r'in_x\d+', 'IN', v)
    return v
def split_top(s):
    out, d, cur = [], 0, ''
    for ch in s:
        if ch in '{(': d += 1
        elif ch in '})': d -= 1
        if ch == '|' and d == 0:
            out.append(cur); cur = ''
        else: cur += ch
    out.append(cur)
    return out
def tagged(v):
    u = _S.unH(v)
    return u.startswith('phi{') and u.endswith('}') and '@@' in u
def cases_of(v, e):
    """value string + edge formula e -> [(value, formula)]; a tagged phi expands into its (absolute) cases"""
    if tagged(v):
        u = _S.unH(v)
        res = []
        for it in split_top(u[4:-1]):
            val, _, fid = it.rpartition('@@')
            if fid.isdigit(): res.append((val, int(fid)))
            else: res.append((it, e))      # untagged item (legacy loop-header merge flattened it): opaque
        return res, True
    return [(v, e)], False

TAGRE = re.compile(r'phi\{([^{}]*@[^{}]*)\}')
UNTRE = re.compile(r'phi\{[^{}@]*\}')
CONSTRE = re.compile(r'^cmp([wx])\((-?\d+),(-?\d+)\):(eq|lt|gt|lo|hi|mi)$')
BITRE = re.compile(r'^bit\((-?\d+),(\d+)\):z$')
def atom2(kk, truth):
    """atom with constant folding (case-split phi{0|1} booleans produce atoms on constants)"""
    m = CONSTRE.match(kk)
    if m:
        bits = 32 if m.group(1) == 'w' else 64
        a, b = int(m.group(2)), int(m.group(3))
        ua, ub = a & ((1 << bits) - 1), b & ((1 << bits) - 1)
        sa = ua - (1 << bits) if ua >> (bits - 1) else ua
        sb = ub - (1 << bits) if ub >> (bits - 1) else ub
        c = m.group(4)
        v = {'eq': sa == sb, 'lt': sa < sb, 'gt': sa > sb, 'lo': ua < ub, 'hi': ua > ub, 'mi': ((ua - ub) & ((1 << bits) - 1)) >> (bits - 1) == 1}[c]
        return 1 if (v == truth) else 0
    m = BITRE.match(kk)
    if m:
        v = ((int(m.group(1)) >> int(m.group(2))) & 1) == 0
        return 1 if (v == truth) else 0
    return atom(kk, truth)

def find_phi(s):
    i = s.find('phi{')
    if i < 0: return None
    d = 0
    for k in range(i + 3, len(s)):
        if s[k] == '{': d += 1
        elif s[k] == '}':
            d -= 1
            if d == 0: return i, k + 1, s[i + 4:k]
    return None
def collapse_all(s):
    while True:
        r = find_phi(s)
        if r is None: return s
        s = s[:r[0]] + 'PHI' + s[r[1]:]
def expand(v, cap=96):
    """outermost-first case split of tagged phi{v@id|..}; each item keeps its own absolute formula; untagged (loop) phi -> PHI"""
    stack = [(_S.full(v), 1)]; out = []
    while stack:
        st, f = stack.pop()
        if len(out) + len(stack) > cap:
            out.append((collapse_all(st), f)); continue
        r = find_phi(st)
        if r is None: out.append((st, f)); continue
        a, b, content = r
        items = split_top(content)
        if all(re.search(r'@@\d+$', it) for it in items):
            for it in items:
                val, _, fid = it.rpartition('@@')
                f2 = AND(f, int(fid))
                if f2 != 0: stack.append((st[:a] + val + st[b:], f2))
        else:
            stack.append((st[:a] + 'PHI' + st[b:], f))
    res = []
    for st, f in out:
        st = re.sub(r'out\([^()]*\)', 'OUT', st); st = re.sub(r'ret1\(', 'RET1(', st); st = re.sub(r'in_x\d+', 'IN', st)
        res.append((st, f))
    return res

class Fn2(Fn):
    def dfs(self):
        color = {}; order = []; back = set()
        stack = [(0, iter(self.succ[0]))]; color[0] = 1
        while stack:
            b, it = stack[-1]; adv = False
            for t, k in it:
                if t is None: continue
                if color.get(t, 0) == 0:
                    color[t] = 1; stack.append((t, iter(self.succ[t]))); adv = True; break
                elif color[t] == 1: back.add((b, t))
            if not adv:
                color[b] = 2; order.append(b); stack.pop()
        order.reverse()
        return order, back
    def analyse(self):
        nb = len(self.blocks)
        order, back = self.dfs()
        seen = set(order)
        order = order + [b for b in range(nb) if b not in seen]
        ins = [None] * nb; outs = [None] * nb; R = {0: 1}
        e0 = St()
        for k in range(8): e0.r[k] = 'a%d' % k
        for k in range(19, 30): e0.r[k] = 'in_x%d' % k
        e0.r['sp'] = 'SP+0'
        def elit(p, k):
            a, m, ops, rel = self.rows[self.blocks[p][1] - 1]
            if outs[p] is not None and (m.startswith('b.') or m in ('cbz', 'cbnz', 'tbz', 'tbnz')):
                key, t = self.brlit(outs[p], m, ops)
                tt = t if k == 'T' else (not t)
                r = 0
                for kk, ff in expand(key):
                    r = OR(r, AND(ff, atom2(kk, tt)))
                return r
            return 1
        def tmerge(b, preds):
            sts = [outs[p] for p, k in preds]
            if len(preds) == 1: return sts[0].copy()
            if any((p, b) in back for p, k in self.pred[b]):
                return merge4(sts)
            # per-pred formula of "this value arrives via p": absolute reach of p AND the edge literal
            E = [AND(R.get(p, 0), elit(p, k)) for p, k in preds]
            LIT = [elit(p, k) for p, k in preds]
            n = merge4(sts)            # flags / lastcall / esc handled the legacy way
            def comb(vals):
                if all(v == vals[0] for v in vals): return vals[0]
                grp = {}
                for v, e, l in zip(vals, E, LIT):
                    cs, tg = cases_of(v, e)
                    for vv, ff in cs:
                        f2 = AND(ff, l) if tg else ff
                        grp[vv] = OR(grp.get(vv, 0), f2)
                if len(grp) == 1: return next(iter(grp))
                return H4('phi{' + '|'.join('%s@@%d' % (vv, grp[vv]) for vv in sorted(grp)) + '}')
            for k in set().union(*[s.r.keys() for s in sts]):
                n.r[k] = comb([s.r.get(k, 'U') for s in sts])
            for k in set().union(*[s.stk.keys() for s in sts]):
                n.stk[k] = comb([s.stk.get(k, 'U') for s in sts])
            return n
        for it in range(60):
            changed = False
            for b in order:
                if b == 0: sin = e0.copy()
                else:
                    preds = [(p, k) for p, k in self.pred[b] if outs[p] is not None]
                    if not preds: continue
                    sin = tmerge(b, preds)
                    f = 0
                    for p, k in self.pred[b]:
                        if (p, b) in back or p not in R or outs[p] is None: continue
                        f = OR(f, AND(R[p], elit(p, k)))
                    R[b] = f
                if ins[b] is not None and ins[b].key() == sin.key(): continue
                ins[b] = sin
                outs[b] = self.run(b, sin.copy())
                changed = True
            if not changed: break
        self.ins = ins; self.R = R
        facts = []; self.outfl = {}
        for b in range(nb):
            if ins[b] is None: continue
            st = ins[b].copy(); s, e = self.blocks[b]
            for i in range(s, e):
                if i == e - 1: self.lastst = st.copy()
                self.step(self.rows[i], st, facts, b)
            self.outfl[b] = self.lastst
        self.G = [frozenset()] * nb
        return facts

def reachf(fn):
    nb = len(fn.blocks)
    color = {}; order = []; back = set()
    stack = [(0, iter(fn.succ[0]))]; color[0] = 1
    while stack:
        b, it = stack[-1]
        adv = False
        for t, k in it:
            if t is None: continue
            if color.get(t, 0) == 0:
                color[t] = 1; stack.append((t, iter(fn.succ[t]))); adv = True; break
            elif color[t] == 1:
                back.add((b, t))
        if not adv:
            color[b] = 2; order.append(b); stack.pop()
    order.reverse()
    lits = {}
    for b in range(nb):
        if fn.ins[b] is None: continue
        a, m, ops, rel = fn.rows[fn.blocks[b][1] - 1]
        if m.startswith('b.') or m in ('cbz', 'cbnz', 'tbz', 'tbnz'):
            k, t = fn.brlit(fn.outfl[b], m, ops)
            k = nphi2(k)   # untagged (loop) phi forms normalised
            lits[(b, 'T')] = atom(k, t); lits[(b, 'F')] = atom(k, not t)
    R = {0: 1}
    for b in order:
        if b == 0: continue
        f = 0
        for p, k in fn.pred[b]:
            if (p, b) in back or p not in R: continue
            f = OR(f, AND(R[p], lits.get((p, k), 1)))
        R[b] = f
    return R

def collect(fn):
    facts = fn.analyse()
    R = fn.R if hasattr(fn, 'R') else reachf(fn)   # -L (legacy engine): reach from the plain forward pass
    by = collections.defaultdict(lambda: 0)
    first = {}
    for f, b, a in facts:
        for ff, cf in expand(f):
            by[ff] = OR(by[ff], AND(R.get(b, 0), cf)); first.setdefault(ff, a)
    return by, first

EFF = re.compile(r'^(call|st|stkarg|atomic|msr|tailcall|trap|barrier|insn)\(')
def main():
    args = [x for x in sys.argv[1:] if not x.startswith('-')]
    sk, ok = args[0], args[1]
    es, eo = Elf(sk), Elf(ok)
    for fname in args[2:]:
        rs, ro = listing(sk, fname, es), listing(ok, fname, eo)
        ar = {}; arity_pass(rs, ar); arity_pass(ro, ar)
        FN = Fn if '-L' in sys.argv else Fn2
        fs, fo = FN(rs, ar), FN(ro, ar)
        (RS, AS), (RO, AO) = collect(fs), collect(fo)
        iseff = lambda f: EFF.match(unH(f)) is not None
        both = [f for f in RS if f in RO and iseff(f)]
        diff = [f for f in both if RS[f] != RO[f]]
        so = [f for f in RS if iseff(f) and f not in RO]; oo = [f for f in RO if iseff(f) and f not in RS]
        print('=== %s: stock %d blocks, ours %d; eff facts %d/%d; both %d; reach-formula-diff %d; only-stock %d only-ours %d' % (
            fname, len(fs.blocks), len(fo.blocks), sum(1 for f in RS if iseff(f)), sum(1 for f in RO if iseff(f)), len(both), len(diff), len(so), len(oo)))
        for f in sorted(diff, key=lambda f: AS[f]):
            print('  R-DIFF S@%x O@%x %s' % (AS[f], AO[f], fmt(f)[:150]))
            a, b = AND(RS[f], NOT(RO[f])), AND(RO[f], NOT(RS[f]))
            if '-q' in sys.argv: continue
            if '-d' in sys.argv:   # atom-level difference of the first stock-only / ours-only cube
                ca, cb = cubes(a, 1), cubes(b, 1)
                if ca and cb:
                    sa = set((VARN[v], t) for v, t in ca[0]); sb = set((VARN[v], t) for v, t in cb[0])
                    for k, t in sorted(sa - sb): print('      S-atom %s%s' % ('' if t else '!', fmt(k)[:230]))
                    for k, t in sorted(sb - sa): print('      O-atom %s%s' % ('' if t else '!', fmt(k)[:230]))
                continue
            if a: print('      stock-only paths: ' + short(a)[:260])
            if b: print('      ours-only  paths: ' + short(b)[:260])
        for f in sorted(so, key=lambda f: AS[f]):
            print('  S-ONLY @%x %s' % (AS[f], fmt(f)[:150]) + ('' if '-q' in sys.argv else '  when ' + short(RS[f])[:200]))
        for f in sorted(oo, key=lambda f: AO[f]):
            print('  O-ONLY @%x %s' % (AO[f], fmt(f)[:150]) + ('' if '-q' in sys.argv else '  when ' + short(RO[f])[:200]))
        print('  PATHSUM %s: eff-both-equal-reach %d, reach-diff %d, only-stock %d, only-ours %d' % (fname, len(both) - len(diff), len(diff), len(so), len(oo)))
if __name__ == '__main__':
    main()
