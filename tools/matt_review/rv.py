#!/usr/bin/env python3
"""rv.py stock.ko built.o stock.dis built.dis [func...]: per function, classify stock vs built:
  IDENT    bytes+relocs identical
  REGONLY  same instruction sequence (movs between registers treated as copies, branch targets compared as instruction indices,
           relocation targets resolved: strings by content, functions/objects by name) AND a consistent value pairing of every
           register use (value numbering): differs only in register allocation  -> proven equal modulo the decoding rules below
  REGCONF  same masked sequence but n uses whose value pairing is inconsistent (listed)  -> read by hand
  ORDER    sequence differs (scheduling/layout/inlining or real): prints the masked multiset diff WITH resolved relocation targets
Decoding rules (limits): arm64 def/use table below; calls clobber x0-x18,x30 and take x0-x7; a value that one side never defined is paired
on first use."""
import collections, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_ko_v2 import Elf, norm  # noqa

REG = re.compile(r'\b([xw](?:\d+|zr)|w?sp)\b')
STORES = re.compile(r'^(str|strb|strh|stur|sturb|sturh|stp|stnp|stlr|stlrb|stlrh|sttr\w*|stlur\w*|stg|stzg)$')
STX = re.compile(r'^st(l)?x(r|p)[bh]?$')
ATOM = re.compile(r'^(ld(add|clr|eor|set|smax|smin|umax|umin)|swp)(a|al|l)?[bh]?$')
STATOM = re.compile(r'^st(add|clr|eor|set|smax|smin|umax|umin)(l)?[bh]?$')
CAS = re.compile(r'^cas(a|al|l)?[bh]?$')
USEONLY = {'cmp', 'cmn', 'tst', 'ccmp', 'ccmn', 'cbz', 'cbnz', 'tbz', 'tbnz', 'br', 'blr', 'ret', 'prfm', 'msr', 'fcmp', 'fccmp'}
DEFUSE1 = {'movk', 'bfi', 'bfxil', 'bfm', 'ins'}
BR = re.compile(r'^(b|b\.\w+|cbn?z|tbn?z|bl)$')


def rn(r):
    if r in ('sp', 'wsp'):
        return 'sp'
    return 'x' + r[1:]


def parse_dis(path):
    """objdump -dr text -> {func: (symaddr, [(addr, mnem, ops)])}"""
    res, cur = {}, None
    for l in open(path):
        m = re.match(r'^([0-9a-f]{16}) <([^>]+)>:$', l)
        if m:
            cur = norm(m.group(2))
            res[cur] = (int(m.group(1), 16), [])
            continue
        if cur is None:
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$', l.rstrip('\n'))
        if m and not l.lstrip().startswith(('0000', 'R_')) and '\t' in l:
            mn = m.group(2)
            if mn.startswith('R_AARCH64') or re.match(r'^[0-9a-f]{16}:', m.group(1)):
                continue
            ops = re.sub(r'\s*//.*$', '', m.group(3)).strip()
            res[cur][1].append((int(m.group(1), 16), mn, ops))
    return res


def split_ops(ops):
    out, depth, cur = [], 0, ''
    for ch in ops:
        if ch == '[':
            depth += 1
        elif ch == ']':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def defuse(mn, ops):
    """-> (defs, uses, copy) register lists (normalized), copy = (dst, src) for reg-reg mov"""
    opl = split_ops(ops)
    outside, inside, wb = [], [], False
    for i, o in enumerate(opl):
        if o.startswith('['):
            inside += [rn(r) for r in REG.findall(o) if not r.endswith('zr')]
            if o.endswith('!') or (i + 1 < len(opl) and opl[i + 1].startswith('#')):
                wb = True
        else:
            outside += [rn(r) for r in REG.findall(o) if not r.endswith('zr')]
    base = inside[:1]
    if mn == 'mov' and len(opl) == 2 and REG.fullmatch(opl[0]) and REG.fullmatch(opl[1]) and not opl[1].endswith('zr'):
        return [], [], (rn(opl[0]), rn(opl[1]))
    if STORES.match(mn):
        return (base if wb else []), outside + inside, None
    if STX.match(mn):
        return outside[:1], outside[1:] + inside, None
    if ATOM.match(mn):
        return outside[1:2], outside[:1] + inside, None
    if STATOM.match(mn) or mn in USEONLY:
        return [], outside + inside, None
    if CAS.match(mn):
        return outside[:1], outside + inside, None
    if mn.startswith('ld'):
        return outside + (base if wb else []), inside, None
    if mn in DEFUSE1:
        return outside[:1], outside + inside, None
    if mn in ('mrs',):
        return outside[:1], [], None
    if not outside and not inside:
        return [], [], None
    return outside[:1], outside[1:] + inside, None


class Side:
    def __init__(self, tag):
        self.tag = tag
        self.n = 0
        self.val = {}
        for r in ['x%d' % i for i in range(8)] + ['sp', 'x29', 'x30', 'x18']:
            self.val[r] = 'in:' + r

    def get(self, r):
        if r not in self.val:
            self.val[r] = '%s:undef:%s' % (self.tag, r)
        return self.val[r]

    def fresh(self):
        self.n += 1
        return '%s:%d' % (self.tag, self.n)


def emit(insns, symaddr, size, rels):
    """-> list of (token, mnem, ops, addr) with movs as copies kept separately (token None)"""
    relat = collections.defaultdict(list)
    for off, t, d in rels:
        relat[off].append(d.split('\x1e')[0])
    out = []
    for addr, mn, ops in insns:
        off = addr - symaddr
        if off < 0 or off >= size:
            continue
        r = relat.get(off)
        o = ops
        if BR.match(mn):
            m = re.search(r'0x([0-9a-f]+)(\s*<[^>]*>)?\s*$', o)
            if m:
                tgt = int(m.group(1), 16) - symaddr
                o = o[:m.start()] + ('@%d' % tgt if 0 <= tgt < size else '@ext')
        o = re.sub(r'\s*<[^>]*>', '', o)
        if r:
            if mn == 'adrp':
                o = re.sub(r'0x[0-9a-f]+$', '', o)
            if mn in ('bl', 'b'):
                o = ''
            o += ' {' + '|'.join(r) + '}'
        out.append((off, mn, o))
    return out


def analyse(sa, sb):
    """sa/sb: emitted lists. returns (status, details)"""
    def stream(lst, side):
        res = []
        idxmap = {}
        for off, mn, o in lst:
            idxmap.setdefault(off, len(res))
            d, u, cp = defuse(mn, o.split(' {')[0])
            if cp:
                side.val[cp[0]] = side.get(cp[1])
                continue
            masked = REG.sub(lambda m: 'SP' if m.group(1) in ('sp', 'wsp') else ('ZR' if m.group(1).endswith('zr') else 'R'), o)
            res.append([mn, masked, d, u, off])
        return res, idxmap

    A, B = Side('s'), Side('b')
    ra, ia = stream(sa, A)
    rb, ib = stream(sb, B)

    def bt(tok, im):
        m = re.search(r'@(\d+)', tok)
        if m:
            t = int(m.group(1))
            ks = sorted(k for k in im if k >= t)
            return tok[:m.start()] + '@#%d' % (im[ks[0]] if ks else -1)
        return tok
    ma = [bt(x[0] + ' ' + x[1], ia) for x in ra]
    mb = [bt(x[0] + ' ' + x[1], ib) for x in rb]
    if ma != mb:
        return 'ORDER', (ma, mb)
    # value pairing walk (recompute values in order, copies applied inline)
    A, B = Side('s'), Side('b')
    P, Q = {}, {}
    for r in A.val:
        P[A.val[r]] = B.val[r]
        Q[B.val[r]] = A.val[r]
    conf = []
    ja = jb = 0
    la, lb = sa, sb
    # re-walk with copies, in lockstep on non-copy instructions
    def it(lst):
        for off, mn, o in lst:
            d, u, cp = defuse(mn, o.split(' {')[0])
            yield off, mn, o, d, u, cp
    ga, gb = list(it(la)), list(it(lb))
    i = j = 0
    k = 0
    while i < len(ga) or j < len(gb):
        while i < len(ga) and ga[i][5]:
            A.val[ga[i][5][0]] = A.get(ga[i][5][1]); i += 1
        while j < len(gb) and gb[j][5]:
            B.val[gb[j][5][0]] = B.get(gb[j][5][1]); j += 1
        if i >= len(ga) or j >= len(gb):
            break
        oa, mna, opa, da, ua, _ = ga[i]
        ob, mnb, opb, db, ub, _ = gb[j]
        if mna == 'bl':
            ua = ['x%d' % q for q in range(8)]
            ub = ua
        # uses
        if mna != 'bl':
            if len(ua) != len(ub):
                conf.append((k, oa, ob, mna + ' ' + opa, mnb + ' ' + opb, 'nuse'))
            for x, y in zip(ua, ub):
                vx, vy = A.get(x), B.get(y)
                if vx in P:
                    if P[vx] != vy:
                        conf.append((k, oa, ob, mna + ' ' + opa, mnb + ' ' + opb, '%s=%s vs %s=%s' % (x, vx, y, vy)))
                elif vy in Q:
                    if not (x.startswith('x') and int(x[1:] or 0) >= 19 and mna.startswith('st')):
                        conf.append((k, oa, ob, mna + ' ' + opa, mnb + ' ' + opb, 'unpaired %s=%s vs %s=%s' % (x, vx, y, vy)))
                else:
                    P[vx], Q[vy] = vy, vx
        else:
            for q in range(8):
                vx, vy = A.get('x%d' % q), B.get('x%d' % q)
                if vx in P and vy in Q and P[vx] != vy:
                    conf.append((k, oa, ob, mna + ' ' + opa, mnb + ' ' + opb, 'callarg x%d' % q))
            da = db = ['x%d' % q for q in range(19)] + ['x30']
        for x, y in zip(da, db):
            vx, vy = A.fresh(), B.fresh()
            A.val[x], B.val[y] = vx, vy
            P[vx], Q[vy] = vy, vx
        i += 1; j += 1; k += 1
    return ('REGONLY' if not conf else 'REGCONF'), conf


def main():
    st, bu, sd, bd = sys.argv[1:5]
    want = set(sys.argv[5:])
    ea, eb = Elf(st), Elf(bu)
    fa, fb = ea.functions(), eb.functions()
    da, db = parse_dis(sd), parse_dis(bd)
    cnt = collections.Counter()
    for f in sorted(fa):
        if want and f not in want:
            continue
        if f not in fb or f not in da or f not in db:
            print('MISSING', f)
            continue
        ca, ra = fa[f][0], fa[f][1]
        cb, rb = fb[f][0], fb[f][1]
        if ca == cb and ra == rb:
            cnt['IDENT'] += 1
            continue
        sa = emit(da[f][1], da[f][0], len(ca), ra)
        sb = emit(db[f][1], db[f][0], len(cb), rb)
        stt, det = analyse(sa, sb)
        cnt[stt] += 1
        if stt == 'REGONLY':
            print('REGONLY', f)
        elif stt == 'REGCONF':
            print('REGCONF', f, len(det))
            for c in det[:40]:
                print('    #%d s+%#x b+%#x | %s | %s | %s' % c)
        else:
            ma, mb = det
            ca_, cb_ = collections.Counter(ma), collections.Counter(mb)
            d = ['%+d %s' % (cb_[x] - ca_[x], x) for x in sorted(set(ca_) | set(cb_)) if ca_[x] != cb_[x] and '@#' not in x]
            nb = sum(abs(cb_[x] - ca_[x]) for x in set(ca_) | set(cb_) if '@#' in x)
            print('ORDER', f, 'len %d/%d' % (len(ma), len(mb)), 'nonbranch-diff %d' % len(d), 'branch-diff %d' % nb)
            for x in d:
                print('    ', x)
    print('SUMMARY', dict(cnt))


main()
