#!/usr/bin/env python3
"""sx.py <lst2.txt> [--offs off_info.txt] [--nm nm.txt] [--arg0 NAME] : lift an aarch64 function listing (lst2.py format) to a
block-wise pseudo-C with symbolic expressions (G30e). Registers are tracked as expression strings; field names of x0 (the info struct) come from
btfflat.py output. Printed per basic block: in-edges with the exact branch condition text, phi merges, stores to non-stack memory, calls with the
argument registers that were set, the block terminator. Reading aid, NOT a proof tool: no width wrap, no alias analysis, loops are not unrolled.
"""
import re, sys, collections

def parse_args():
    a = sys.argv[1:]
    d = {'file': a[0], 'offs': None, 'nm': None, 'arg0': 'info'}
    i = 1
    while i < len(a):
        if a[i] == '--offs': d['offs'] = a[i+1]; i += 2
        elif a[i] == '--nm': d['nm'] = a[i+1]; i += 2
        elif a[i] == '--arg0': d['arg0'] = a[i+1]; i += 2
        elif a[i] == '--jt': d.setdefault('jtl', []).append(a[i+1]); i += 2   # base:file[:rel32] ; halfwords: target = base + 4*h ; rel32: target = base + signed int
        elif a[i] == '--init': d.setdefault('init', []).append(a[i+1]); i += 2
        else: i += 1
    return d

CFG = parse_args()
OFFS = {}
if CFG['offs']:
    for l in open(CFG['offs']):
        p = l.split()
        if len(p) >= 2 and not l.startswith('#'):
            OFFS[int(p[0], 16)] = p[1]
SYMS = {}
if CFG['nm']:
    for l in open(CFG['nm']):
        p = l.split()
        if len(p) == 4 and p[2] in 'tT':
            SYMS[int(p[0], 16)] = p[3]

def num(s):
    s = s.strip().lstrip('#')
    return int(s, 0)

def split_ops(s):
    out, cur, d = [], '', 0
    for ch in s:
        if ch == '[': d += 1
        if ch == ']': d -= 1
        if ch == ',' and d == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def fmt(v):
    if isinstance(v, int):
        v &= 0xFFFFFFFFFFFFFFFF
        if v >= 1 << 63: v -= 1 << 64
        return str(v) if abs(v) < 65536 else hex(v)
    return v

def par(v):
    v = fmt(v)
    return v if re.fullmatch(r'[\w.@#$\[\]>\-]+|\([^()]*\)|".*"', v) and not re.search(r'[+*<>=!&|^/ -]\S', v.lstrip('-')) else '(' + v + ')'

def add(a, b):
    if isinstance(a, int) and isinstance(b, int): return (a + b) & 0xFFFFFFFFFFFFFFFF
    if isinstance(b, int) and b == 0: return a
    if isinstance(a, int) and a == 0: return b
    if isinstance(b, int):
        if b >= (1 << 63): b -= 1 << 64
        m = re.fullmatch(r'(.*[^+\-\d(])([+-])(\d+)', fmt(a))
        if m and '?' not in m.group(1) and ' ' not in m.group(1):
            c = int(m.group(3)) * (1 if m.group(2) == '+' else -1) + b
            return m.group(1) if c == 0 else m.group(1) + ('+' if c > 0 else '-') + str(abs(c))
        return par(a) + ('+' if b >= 0 else '-') + str(abs(b))
    # magic division recognition: ((X*M)>>S) + ((X*M)>>>63)  ->  X/D
    ma = re.fullmatch(r'\((.+)\*(0x[0-9a-f]+|\d+)\)>>(\d+)', fmt(a)); mb = re.fullmatch(r'\((.+)\*(0x[0-9a-f]+|\d+)\)>>>63', fmt(b))
    if ma and mb and ma.group(1) == mb.group(1) and ma.group(2) == mb.group(2):
        M, S = int(ma.group(2), 0), int(ma.group(3))
        Dv = (1 << S) / M
        if abs(Dv - round(Dv)) < 0.01 and round(Dv) > 1: return '(%s/%d)' % (ma.group(1), round(Dv))
    return par(a) + '+' + par(b)

def sub(a, b):
    if isinstance(a, int) and isinstance(b, int): return (a - b) & 0xFFFFFFFFFFFFFFFF
    if isinstance(b, int): return add(a, -b)
    if isinstance(a, int) and a == 0: return '-' + par(b)
    return par(a) + '-' + par(b)

def mul(a, b):
    if isinstance(a, int) and isinstance(b, int): return (a * b) & 0xFFFFFFFFFFFFFFFF
    for x, y in ((a, b), (b, a)):
        if isinstance(x, int) and x == 1: return y
        if isinstance(x, int) and x == 0: return 0
    return par(a) + '*' + par(b)

def lsl(a, n):
    if isinstance(a, int): return (a << n) & 0xFFFFFFFFFFFFFFFF
    return par(a) + '<<' + str(n)

def asr(a, n):
    if isinstance(a, int):
        if a >= 1 << 63: a -= 1 << 64
        return (a >> n) & 0xFFFFFFFFFFFFFFFF
    m = re.fullmatch(r'\((.+) < 0 \? (.+)\+1 : (.+)\)', fmt(a))
    if m and m.group(2) == m.group(3) == m.group(1) and n >= 1: return '(%s/%d)' % (par(m.group(1)), 1 << n)
    m = re.fullmatch(r'(.+)\+\((.+)>>>31\)', fmt(a))
    if m and m.group(1) == m.group(2) and n >= 1: return '(%s/%d)' % (par(m.group(1)), 1 << n)
    return par(a) + '>>' + str(n)

def lsr(a, n):
    if isinstance(a, int): return (a & 0xFFFFFFFFFFFFFFFF) >> n
    return par(a) + '>>>' + str(n)

NEG = {'eq': 'ne', 'ne': 'eq', 'hs': 'lo', 'cs': 'lo', 'lo': 'hs', 'cc': 'hs', 'mi': 'pl', 'pl': 'mi', 'vs': 'vc', 'vc': 'vs',
       'hi': 'ls', 'ls': 'hi', 'ge': 'lt', 'lt': 'ge', 'gt': 'le', 'le': 'gt'}

def nzcv_eval(n, cc):
    N, Z, C, V = (n >> 3) & 1, (n >> 2) & 1, (n >> 1) & 1, n & 1
    return {'eq': Z == 1, 'ne': Z == 0, 'hs': C == 1, 'cs': C == 1, 'lo': C == 0, 'cc': C == 0, 'mi': N == 1, 'pl': N == 0, 'vs': V == 1, 'vc': V == 0,
            'hi': C == 1 and Z == 0, 'ls': not (C == 1 and Z == 0), 'ge': N == V, 'lt': N != V, 'gt': Z == 0 and N == V, 'le': not (Z == 0 and N == V)}[cc]

def cond_str(fl, cc):
    k = fl[0]
    if k == 'cmp':
        a, b = fl[1], fl[2]
        A, B = par(a), par(b)
        if isinstance(b, int) and b == 0 and cc in ('mi', 'pl'): return '%s %s 0' % (A, '<' if cc == 'mi' else '>=')
        op = {'eq': '==', 'ne': '!=', 'hs': '>=u', 'cs': '>=u', 'lo': '<u', 'cc': '<u', 'hi': '>u', 'ls': '<=u', 'ge': '>=', 'lt': '<', 'gt': '>', 'le': '<='}.get(cc)
        if op: return '%s %s %s' % (A, op, B)
        return '%s(%s-%s)' % (cc, A, B)
    if k == 'tst':
        e = '%s&%s' % (par(fl[1]), par(fl[2]))
        return {'eq': '(%s)==0' % e, 'ne': '(%s)!=0' % e}.get(cc, '%s(%s)' % (cc, e))
    if k == 'res':
        e = par(fl[1])
        return {'eq': '%s==0' % e, 'ne': '%s!=0' % e, 'mi': '%s<0' % e, 'pl': '%s>=0' % e, 'lt': '%s<0' % e, 'ge': '%s>=0' % e,
                'gt': '%s>0' % e, 'le': '%s<=0' % e}.get(cc, '%s(%s)' % (cc, e))
    if k == 'cc':  # ccmp: (prevcond ? cmp(a,b) : const nzcv)
        _, prev, pcc, inner, nz = fl
        P = cond_str(prev, pcc)
        X = cond_str(inner, cc)
        if nzcv_eval(nz, cc): return '!(%s) || (%s)' % (P, X)
        return '(%s) && (%s)' % (P, X)
    return '?%s' % cc

class Blk:
    def __init__(s, a):
        s.a, s.ins, s.succ, s.pred, s.lines, s.term = a, [], [], [], [], ''

def load_listing(path):
    rows = []
    for l in open(path, encoding='latin1'):
        l = l.rstrip('\n')
        m = re.match(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$', l)
        if not m: continue
        off = int(m.group(1), 16); mn = m.group(2); rest = m.group(3)
        # operands vs comment: operands end at first run of >=3 spaces in the column-aligned dump
        parts = re.split(r'\s{3,}', rest, maxsplit=1)
        ops = parts[0].strip(); cmt = parts[1].strip() if len(parts) > 1 else ''
        if mn in ('adrp', 'bl') and not cmt and ops and not ops.startswith('x'):
            cmt = ops; ops = ''
        rows.append((off, mn, ops, cmt))
    return rows

def regnum(r):
    r = r.strip()
    if r in ('wzr', 'xzr'): return 'zr'
    if r in ('sp', 'wsp'): return 'sp'
    m = re.fullmatch(r'[xw](\d+)', r)
    return int(m.group(1)) if m else None

def main():
    rows = load_listing(CFG['file'])
    idx = {r[0]: i for i, r in enumerate(rows)}
    # block boundaries
    leaders = {rows[0][0]}
    for off, mn, ops, cmt in rows:
        if mn in ('b', 'cbz', 'cbnz', 'tbz', 'tbnz') or mn.startswith('b.'):
            t = re.search(r'@([0-9a-f]+)', ops)
            if t: leaders.add(int(t.group(1), 16))
    for i, (off, mn, ops, cmt) in enumerate(rows):
        if (mn in ('b', 'ret', 'brk', 'br') or mn.startswith('b.') or mn in ('cbz', 'cbnz', 'tbz', 'tbnz')) and i + 1 < len(rows):
            leaders.add(rows[i + 1][0])
    blocks = collections.OrderedDict()
    cur = None
    for off, mn, ops, cmt in rows:
        if off in leaders:
            cur = Blk(off); blocks[off] = cur
        cur.ins.append((off, mn, ops, cmt))
    # successors
    for b in blocks.values():
        off, mn, ops, cmt = b.ins[-1]
        nxt = None
        i = idx[off]
        if i + 1 < len(rows): nxt = rows[i + 1][0]
        t = re.search(r'@([0-9a-f]+)', ops); t = int(t.group(1), 16) if t else None
        if mn == 'b':
            b.succ = [(t, 'always')] if t is not None else []
        elif mn == 'br':
            # jump table made of consecutive 'b @X' rows, address taken by the nearest preceding adr xN, @T
            T = None
            for (o2, m2, p2, c2) in reversed(b.ins):
                mt = re.match(r'x\d+,?\s*@([0-9a-f]+)', p2) if m2 == 'adr' else None
                if mt: T = int(mt.group(1), 16); break
            b.succ = []
            jtm = [j for j in CFG.get('jtl', []) if int(j.split(':')[0], 16) == T]
            if T is not None and jtm:
                pj = jtm[0].split(':')
                hw = [int(x) for x in open(pj[1]).read().split()]
                rel = len(pj) > 2 and pj[2] == 'rel32'
                cases = collections.OrderedDict()
                for k, h in enumerate(hw): cases.setdefault(T + (h if rel else 4 * h), []).append(k)
                b.cases = cases
                b.succ = [(t, 'case') for t in cases]
            elif T is not None and T in idx:
                k = idx[T]
                while k < len(rows) and rows[k][1] == 'b' and rows[k][2].startswith('@'):
                    b.succ.append((rows[k][0], 'tbl')); k += 1
        elif mn in ('ret', 'brk'):
            b.succ = []
        elif mn.startswith('b.') or mn in ('cbz', 'cbnz', 'tbz', 'tbnz'):
            b.succ = [(t, 'T'), (nxt, 'F')]
        else:
            b.succ = [(nxt, 'fall')] if nxt is not None else []
    # RPO
    order, seen = [], set()
    def dfs(a):
        if a in seen or a not in blocks: return
        seen.add(a)
        for s, _ in blocks[a].succ: dfs(s)
        order.append(a)
    sys.setrecursionlimit(10000)
    dfs(rows[0][0]); order.reverse()
    for a in order:
        for s, k in blocks[a].succ:
            if s in blocks: blocks[s].pred.append((a, k))

    phis = {}
    nphi = [0]
    outstate = {}
    ret_n = [0]
    edgecond = {}

    def new_state():
        return {'r': {}, 'm': {}, 'fl': None}

    def get(st, r, w=None):
        n = regnum(r)
        if n == 'zr': return 0
        if n == 'sp': return 'sp'
        if n is None: return r
        v = st['r'].get(n)
        if v is None: return 'x%d@in' % n if n != 0 or True else None
        return v

    TMP = [0]
    CUR = {'out': None}
    def setr(st, r, v):
        n = regnum(r)
        if n == 'zr' or n is None: return
        if n == 'sp': st['r'][n] = v; return
        if isinstance(v, str) and len(v) > 56 and not v.startswith('"') and CUR['out'] is not None and n != 'sp':
            TMP[0] += 1; nm = 't%d' % TMP[0]
            CUR['out'].append('  %s = %s' % (nm, v))
            v = nm
        st['r'][n] = v

    def operand(st, o):
        o = o.strip()
        if o.startswith('#'): return num(o)
        m = re.fullmatch(r'([xw]\d+|[xw]zr|sp)(?:,\s*(lsl|lsr|asr|sxtw|uxtw|sxtx)\s*(?:#(\d+))?)?', o)
        if m:
            v = get(st, m.group(1))
            if m.group(2):
                n = int(m.group(3) or 0)
                if m.group(2) == 'lsl': v = lsl(v, n)
                elif m.group(2) == 'lsr': v = lsr(v, n)
                elif m.group(2) == 'asr': v = asr(v, n)
                elif m.group(2) in ('sxtw', 'uxtw', 'sxtx'): v = lsl(v, n) if n else v
            return v
        return o

    def parse_shifted(st, ops, i):
        # operand i may be 'x9' followed by a shift operand 'lsl #2' as the next element
        base = get(st, ops[i]) if regnum(ops[i]) is not None else num(ops[i]) if ops[i].startswith('#') else ops[i]
        if i + 1 < len(ops):
            m = re.fullmatch(r'(lsl|lsr|asr|sxtw|uxtw|sxtx|sxth|uxth|sxtb|uxtb)\s*(?:#(\d+))?', ops[i + 1])
            if m:
                n = int(m.group(2) or 0)
                if m.group(1) == 'lsl': base = lsl(base, n)
                elif m.group(1) == 'lsr': base = lsr(base, n)
                elif m.group(1) == 'asr': base = asr(base, n)
                elif n: base = lsl(base, n)
        return base

    def addr_of(st, s):
        # s like '[x19, #0x7f0]' or '[x8, x9, lsl #2]'
        s = s.strip()
        s = s.rstrip('!')
        m = re.fullmatch(r'\[(.*)\]', s)
        inner = split_ops(m.group(1)) if m else [s]
        base = get(st, inner[0])
        off = 0
        mm = re.fullmatch(CFG['arg0'] + r'([+-])(\d+)', base) if isinstance(base, str) else None
        if mm: off += int(mm.group(2)) * (1 if mm.group(1) == '+' else -1); base = CFG['arg0']
        if len(inner) > 1:
            if inner[1].startswith('#'): off += num(inner[1])
            else:
                v = parse_shifted(st, inner, 1)
                base = add(base, v); off = 0
        return base, off

    def memname(base, off):
        if base == CFG['arg0'] and off in OFFS: return '%s.%s' % (CFG['arg0'], OFFS[off])
        if base == CFG['arg0']:
            import bisect
            ks = sorted(OFFS); j = bisect.bisect_right(ks, off) - 1
            if j >= 0 and off - ks[j] <= 60: return '%s.%s+%d' % (CFG['arg0'], OFFS[ks[j]], off - ks[j])
            return '%s+0x%x' % (CFG['arg0'], off)
        if isinstance(base, str):
            m = re.fullmatch(r'sp\+(\d+)', base)
        return '*(%s+0x%x)' % (par(base), off) if off else '*%s' % par(base)

    def stk_key(base, off):
        # normalised sp offset or None
        if base == 'sp': return off
        m = re.fullmatch(r'sp\+(\d+)', fmt(base)) if isinstance(base, str) else None
        if m: return int(m.group(1)) + off
        if isinstance(base, str) and base == 'sp-0': return off
        return None

    def sub_resolve(base):
        return base

    def do_block(b, st):
        out = []
        CUR['out'] = out
        for (off, mn, ops, cmt) in b.ins:
            o = split_ops(ops)
            try:
                step(b, st, off, mn, o, cmt, out)
            except Exception as e:
                out.append('  ?? %x %s %s  [%r]' % (off, mn, ops, e))
        return out

    def step(b, st, off, mn, o, cmt, out):
        R = st['r']
        def g(i): return operand(st, o[i]) if not (regnum(o[i]) is not None and i + 1 < len(o) and re.match(r'(lsl|lsr|asr|sxtw|uxtw|sxtx)', o[i + 1])) else parse_shifted(st, o, i)
        if mn in ('paciasp', 'autiasp', 'nop', 'hint', 'dmb', 'isb', 'prfm', 'bti', 'retaa'): return
        if mn == 'mrs': setr(st, o[0], 'sysreg(%s)' % o[1]); return
        if mn in ('mov', 'movz'):
            setr(st, o[0], operand(st, o[1]) if mn == 'mov' else (num(o[1]) << (num(o[2].split('#')[1]) if len(o) > 2 else 0)))
            return
        if mn == 'movk':
            v = get(st, o[0]); sh = num(o[2].split('#')[1]) if len(o) > 2 and '#' in o[2] else 0
            if isinstance(v, int):
                v = (v & ~(0xFFFF << sh)) | (num(o[1]) << sh); setr(st, o[0], v & (0xFFFFFFFF if o[0].startswith('w') else 0xFFFFFFFFFFFFFFFF))
            else: setr(st, o[0], 'movk(%s)' % o[1])
            return
        if mn == 'mvn': setr(st, o[0], '~' + par(g(1))); return
        if mn == 'adr':
            setr(st, o[0], '@' + o[1].lstrip('@')); return
        if mn == 'adrp':
            setr(st, o[0], cmt if cmt.startswith('"') else '&' + (SYMS.get(int(cmt.split('+0x')[1], 16)) if False else cmt)); return
        if mn == 'add' or mn == 'adds':
            r = add(g(1), g(2) if len(o) < 4 else parse_shifted(st, o, 2));
            if o[0].startswith('w') and isinstance(r, int): r &= 0xFFFFFFFF
            setr(st, o[0], r)
            if mn == 'adds': st['fl'] = ('res', r)
            return
        if mn in ('sub', 'subs'):
            a = g(1); bb = g(2) if len(o) < 4 else parse_shifted(st, o, 2)
            r = sub(a, bb)
            if o[0].startswith('w') and isinstance(r, int): r &= 0xFFFFFFFF
            if regnum(o[0]) == 'sp' or (regnum(o[1]) == 'sp'):
                if mn == 'sub' and regnum(o[0]) == 'sp': R['sp'] = 'sp-%s' % fmt(bb)
            setr(st, o[0], r)
            if mn == 'subs':
                st['fl'] = ('cmp', a, bb)
            return
        if mn in ('cmp', 'cmn'):
            a = g(0); bb = g(1) if len(o) < 3 else parse_shifted(st, o, 1)
            if mn == 'cmn':
                bb = (-bb) if isinstance(bb, int) else '-' + par(bb)
            st['fl'] = ('cmp', a, bb); return
        if mn == 'tst':
            st['fl'] = ('tst', get(st, o[0]), g(1)); return
        if mn in ('ccmp', 'ccmn'):
            a = get(st, o[0]); bb = operand(st, o[1]); nz = num(o[2]); cc = o[3]
            if mn == 'ccmn': bb = (-bb) if isinstance(bb, int) else '-' + par(bb)
            st['fl'] = ('cc', st['fl'], cc, ('cmp', a, bb), nz); return
        if mn in ('mul',): setr(st, o[0], mul(get(st, o[1]), get(st, o[2]))); return
        if mn in ('smull', 'umull'): setr(st, o[0], mul(get(st, o[1]), get(st, o[2]))); return
        if mn in ('madd', 'smaddl', 'umaddl'): setr(st, o[0], add(get(st, o[3]), mul(get(st, o[1]), get(st, o[2])))); return
        if mn in ('msub', 'smsubl'): setr(st, o[0], sub(get(st, o[3]), mul(get(st, o[1]), get(st, o[2])))); return
        if mn == 'mneg': setr(st, o[0], '-' + par(mul(get(st, o[1]), get(st, o[2])))); return
        if mn in ('sdiv', 'udiv'): setr(st, o[0], par(get(st, o[1])) + ('/' if mn == 'sdiv' else '/u') + par(get(st, o[2]))); return
        if mn == 'neg': setr(st, o[0], sub(0, g(1))); return
        if mn in ('lsl', 'lsr', 'asr'):
            a = get(st, o[1]); n = operand(st, o[2])
            if isinstance(n, int): setr(st, o[0], {'lsl': lsl, 'lsr': lsr, 'asr': asr}[mn](a, n))
            else: setr(st, o[0], '%s %s %s' % (par(a), {'lsl': '<<', 'lsr': '>>>', 'asr': '>>'}[mn], par(n)))
            return
        if mn in ('and', 'ands', 'orr', 'eor', 'bic', 'orn'):
            a = get(st, o[1]); bb = g(2) if len(o) < 4 else parse_shifted(st, o, 2)
            op = {'and': '&', 'ands': '&', 'orr': '|', 'eor': '^', 'bic': '&~', 'orn': '|~'}[mn]
            if isinstance(a, int) and isinstance(bb, int):
                r = {'&': a & bb, '|': a | bb, '^': a ^ bb}.get(op, None)
            else: r = None
            if r is None:
                if mn == 'orr' and isinstance(a, int) and a == 0: r = bb
                elif mn in ('and', 'ands') and isinstance(bb, int) and bb == 0xFFFFFFFF and o[0].startswith('w'): r = a
                else: r = par(a) + op + par(bb)
            setr(st, o[0], r)
            if mn == 'ands': st['fl'] = ('res', r)
            return
        if mn in ('sxtw', 'uxtw', 'sxth', 'sxtb', 'uxth', 'uxtb'): setr(st, o[0], get(st, o[1]) if mn in ('sxtw', 'uxtw') else '%s(%s)' % (mn, fmt(get(st, o[1])))); return
        if mn in ('ubfx', 'sbfx', 'ubfiz', 'sbfiz', 'bfi', 'bfxil', 'ubfm', 'sbfm', 'bfm'):
            setr(st, o[0], '%s(%s,%s,%s)' % (mn, fmt(get(st, o[1])), o[2].lstrip('#'), o[3].lstrip('#') if len(o) > 3 else '')); return
        if mn in ('csel', 'csinc', 'csinv', 'csneg'):
            a = get(st, o[1]); bb = get(st, o[2]); cc = o[3]
            c = cond_str(st['fl'], cc)
            if mn == 'csinc': bb = add(bb, 1)
            elif mn == 'csinv': bb = '~' + par(bb)
            elif mn == 'csneg': bb = '-' + par(bb)
            if fmt(a) == fmt(bb): setr(st, o[0], a)
            else: setr(st, o[0], '(%s ? %s : %s)' % (c, fmt(a), fmt(bb)))
            return
        if mn in ('cset', 'csetm'):
            c = cond_str(st['fl'], o[1])
            setr(st, o[0], '(%s)' % c if mn == 'cset' else '-(%s)' % c); return
        if mn in ('cinc', 'cinv', 'cneg'):
            a = get(st, o[1]); c = cond_str(st['fl'], o[2])
            b2 = add(a, 1) if mn == 'cinc' else ('~' + par(a) if mn == 'cinv' else '-' + par(a))
            setr(st, o[0], '(%s ? %s : %s)' % (c, fmt(b2), fmt(a))); return
        if mn in ('ldr', 'ldrb', 'ldrh', 'ldrsw', 'ldrsb', 'ldrsh', 'ldur', 'ldurb', 'ldurh', 'ldursw', 'ldp', 'ldar', 'ldapr', 'ldarb', 'ldaprb', 'ldxr', 'ldaxr'):
            is_pair = mn == 'ldp'
            base, offs = addr_of(st, o[2] if is_pair else o[1])
            tag = {'ldrb': 'u8 ', 'ldurb': 'u8 ', 'ldarb': 'u8 ', 'ldaprb': 'u8 ', 'ldrh': 'u16 ', 'ldurh': 'u16 ', 'ldrsb': 's8 ', 'ldrsh': 's16 '}.get(mn, '')
            for k, rn in enumerate(o[:2] if is_pair else o[:1]):
                a_off = offs + k * (8 if rn.startswith('x') else 4)
                sk = stk_key(base, a_off)
                if sk is not None:
                    v = st['m'].get(sk)
                    if v is None: v = 'stk[%d]' % sk
                    setr(st, rn, v)
                else:
                    nm = memname(base, a_off)
                    if cmt and cmt.startswith('.bss') or (cmt and cmt.startswith('.data')): nm = '@' + (SYMS.get(0) or cmt)
                    elif isinstance(base, str) and base.startswith('&') : nm = '@' + base[1:] + ('+0x%x' % a_off if a_off else '')
                    setr(st, rn, tag + nm)
                    if mn in ('ldar', 'ldapr', 'ldarb', 'ldaprb'): setr(st, rn, 'acq ' + tag + nm)
            return
        if mn in ('str', 'strb', 'strh', 'stur', 'sturb', 'sturh', 'stp', 'stlr', 'stlrb', 'stxr', 'stlxr'):
            is_pair = mn == 'stp'
            base, offs = addr_of(st, o[2] if is_pair else o[1])
            tag = {'strb': 'u8 ', 'sturb': 'u8 ', 'strh': 'u16 ', 'sturh': 'u16 ', 'stlrb': 'u8 '}.get(mn, '')
            for k, rn in enumerate(o[:2] if is_pair else o[:1]):
                a_off = offs + k * (8 if rn.startswith('x') else 4)
                v = get(st, rn)
                sk = stk_key(base, a_off)
                if sk is not None:
                    st['m'][sk] = v
                    if sk < 0x60 and not (regnum(rn) == 'zr' and False):
                        out.append('  STK sp+%#x = %s' % (sk, fmt(v)))
                else:
                    out.append('  ST %s%s = %s' % (tag, memname(base, a_off) if not (isinstance(base, str) and base.startswith('&')) else '@' + base[1:], fmt(v)))
            return
        if mn == 'bl' or mn == 'blr':
            tgt = cmt or (o[0] if o else '?')
            m = re.fullmatch(r'\.text\+0x([0-9a-f]+)', tgt)
            if m: tgt = SYMS.get(int(m.group(1), 16), tgt)
            tgt = re.sub(r'^\.text\.', '', tgt)
            args = []
            for n in range(8):
                v = R.get(n)
                if v is not None and not (isinstance(v, str) and v.startswith('unk')):
                    args.append('a%d=%s' % (n, fmt(v)))
            out.append('  CALL %s(%s)' % (tgt, ', '.join(args)))
            for n in range(0, 19): R.pop(n, None)
            ret_n[0] += 1
            R[0] = 'ret%d:%s' % (ret_n[0], tgt)
            return
        if mn == 'b' and not o[0].startswith('@'):
            tgt = re.sub(r'^\.text\.', '', cmt or o[0])
            m = re.fullmatch(r'\.text\+0x([0-9a-f]+)', tgt)
            if m: tgt = SYMS.get(int(m.group(1), 16), tgt)
            args = ['a%d=%s' % (n, fmt(R[n])) for n in range(8) if n in R]
            out.append('  TAILCALL %s(%s)' % (tgt, ', '.join(args))); return
        if mn == 'ret': out.append('  RET %s' % fmt(R.get(0, 'x0@in'))); return
        if mn == 'brk': out.append('  TRAP %s' % o[0]); return
        if mn in ('b', 'cbz', 'cbnz', 'tbz', 'tbnz') or mn.startswith('b.'): return  # handled by the terminator
        if mn in ('ldaddal', 'ldadd', 'swp', 'cas', 'casal', 'stadd', 'staddl', 'ldsetal', 'ldclral', 'stset', 'stclr'):
            out.append('  ATOMIC %s %s' % (mn, ' '.join(o))); return
        out.append('  ?? %x %s %s' % (off, mn, ' '.join(o)))

    states_out = {}
    OUT = []
    for a in order:
        b = blocks[a]
        preds = [(p, k) for p, k in b.pred if p in states_out]
        if not preds:
            st = new_state(); st['r'][0] = CFG['arg0']
            for kv in CFG.get('init', []):
                k, v = kv.split('=', 1); st['r'][int(k.lstrip('xw'))] = v
            hdr = '  [entry]'
        else:
            sts = [(p, k, states_out[p]) for p, k in preds]
            st = new_state()
            regs = set().union(*[set(s['r']) for _, _, s in sts])
            hdr_phi = []
            for r in regs:
                vals = [s['r'].get(r) for _, _, s in sts]
                if all(v == vals[0] for v in vals): st['r'][r] = vals[0]
                else:
                    if any(v is None for v in vals): vals = [v if v is not None else 'x%s@in' % r for v in vals]
                    nphi[0] += 1; nm = 'φ%d' % nphi[0]
                    st['r'][r] = nm
                    hdr_phi.append('%s(r%s) = %s' % (nm, r, ' | '.join('B%x:%s' % (p, fmt(v)) for (p, _, _), v in zip(sts, vals))))
            keys = set().union(*[set(s['m']) for _, _, s in sts])
            for k in keys:
                vals = [s['m'].get(k, 'stk[%d]' % k) for _, _, s in sts]
                if all(v == vals[0] for v in vals): st['m'][k] = vals[0]
                else:
                    nphi[0] += 1; nm = 'φ%d' % nphi[0]; st['m'][k] = nm
                    hdr_phi.append('%s(stk%d) = %s' % (nm, k, ' | '.join('B%x:%s' % (p, fmt(v)) for (p, _, _), v in zip(sts, vals))))
            st['fl'] = sts[0][2]['fl'] if len(sts) == 1 else None
            if len(sts) > 1:
                fs = [s['fl'] for _, _, s in sts]
                st['fl'] = fs[0] if all(f == fs[0] for f in fs) else None
            conds = []
            for p, k, s in sts:
                conds.append('B%x:%s' % (p, edgecond.get((p, a), '')))
            hdr = '  <- ' + ' ; '.join(conds)
            if hdr_phi: hdr += '\n  ' + '\n  '.join(hdr_phi)
        out = do_block(b, st)
        # terminator condition
        off, mn, ops, cmt = b.ins[-1]
        term = ''
        if mn.startswith('b.') or mn in ('cbz', 'cbnz', 'tbz', 'tbnz'):
            o = split_ops(ops)
            if mn.startswith('b.'): c_t = cond_str(st['fl'], mn[2:]) if st['fl'] else '?flags'; c_f = cond_str(st['fl'], NEG[mn[2:]]) if st['fl'] else '?flags'
            else:
                v = fmt(get(st, o[0])) if regnum(o[0]) is not None else o[0]
                if mn == 'cbz': c_t, c_f = '%s == 0' % v, '%s != 0' % v
                elif mn == 'cbnz': c_t, c_f = '%s != 0' % v, '%s == 0' % v
                else:
                    bit = num(o[1])
                    c_t, c_f = (('bit%d(%s) == 0' % (bit, v)), ('bit%d(%s) != 0' % (bit, v))) if mn == 'tbz' else (('bit%d(%s) != 0' % (bit, v)), ('bit%d(%s) == 0' % (bit, v)))
            (t, _), (f, _) = b.succ
            edgecond[(a, t)] = c_t if t != f else ''
            edgecond[(a, f)] = c_f if t != f else ''
            term = '  IF (%s) -> B%x else B%x' % (c_t, t, f)
        elif mn == 'br' and getattr(b, 'cases', None):
            for t, ks in b.cases.items(): edgecond[(a, t)] = 'idx in %s' % (','.join(map(str, ks)) if len(ks) < 12 else '%d values' % len(ks))
            term = '  SWITCH ' + ' '.join('%s->B%x' % (','.join(map(str, ks)) if len(ks) < 6 else '%d..' % ks[0], t) for t, ks in b.cases.items())
        elif mn == 'b' and b.succ:
            term = '  GOTO B%x' % b.succ[0][0]
        elif b.succ and b.succ[0][1] == 'fall':
            term = '  (fall) B%x' % b.succ[0][0]
        states_out[a] = st
        OUT.append('B%x:%s' % (a, hdr))
        OUT.extend(out)
        if term: OUT.append(term)
    # unreachable blocks
    for a in blocks:
        if a not in states_out: OUT.append('B%x: UNREACHED' % a)
    # keep only phi definitions that are (transitively) read by an effect / condition line
    lines = list(OUT)
    pdef = {}
    for l in lines:
        m = re.match(r'^  (φ\d+)\((?:r\d+|stk-?\d+)\) = (.*)$', l)
        if m: pdef[m.group(1)] = set(re.findall(r'φ\d+', m.group(2)))
    live = set()
    for l in lines:
        if re.match(r'^  φ\d+\((?:r\d+|stk-?\d+)\) = ', l): continue
        live |= set(re.findall(r'φ\d+', l))
    todo = list(live)
    while todo:
        p = todo.pop()
        for q in pdef.get(p, ()):
            if q not in live: live.add(q); todo.append(q)
    lines = [l for l in lines if not (re.match(r'^  φ\d+\((?:r\d+|stk-?\d+)\) = ', l) and re.match(r'^  (φ\d+)', l).group(1) not in live)]
    print(chr(10).join(lines))

main()
