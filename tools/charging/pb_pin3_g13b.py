#!/usr/bin/env python3
"""pb_pin.py <dump_dir> <logfmt_regex_prefix> <src.c> [more.c ...]

Pins __LINE__ of every log statement (mca_log_xxx / pr_xxx style: <logfn>("fmt", ...)) of the given C files to the value the
stock module had, by inserting `#line N` before the statement.  Matching key = (enclosing function name, fmt string):
the stock call sites are found in <dump_dir>/dis.txt by tracking the argument registers (x0 = fmt, x1 = func, w2 = line)
at each `bl __mca_log_*`.  Usage example:
  pb_pin.py dump/sc8581 '[cp_sc8581]%s:%d ' sc8581.c
The prefix is prepended to the C literal to get the stock format string.  Use --logfn REGEX to change the call names.
Idempotent (existing `#line` lines are removed first)."""
import re, sys, ast, collections

def load_stock(dump, calls=r'__mca_log_(err|info|debug)'):
    ro = {}
    for l in open(dump + '/rodata.txt', encoding='utf-8', errors='replace'):
        m = re.match(r'^\.rodata\+0x([0-9a-f]+)\t(.*)$', l.rstrip('\n'))
        if m:
            try:
                ro[int(m.group(1), 16)] = ast.literal_eval(m.group(2))
            except Exception:
                pass
    import bisect
    starts = sorted(ro)
    ro_exact = ro

    class RO(dict):
        def get(self, off, d=None):
            if off in ro_exact:
                return ro_exact[off]
            k = bisect.bisect_right(starts, off) - 1   # tail-merged strings: pointer inside a longer string
            if k >= 0:
                st = starts[k]
                if off - st <= len(ro_exact[st]):
                    return ro_exact[st][off - st:]
            return d
    ro = RO()
    lines = open(dump + '/dis.txt').read().split('\n')
    res = collections.defaultdict(set)   # (func, fmt) -> {line}
    funcs = {}
    cur = None
    i = 0
    ins_list = []
    # build per-function instruction list with relocs
    cur = None
    fn = {}
    for l in lines:
        m = re.match(r'^([0-9a-f]{16}) <(.+)>:$', l)
        if m:
            cur = m.group(2) + '@' + m.group(1); fn[cur] = []; continue
        if cur is None: continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(\S.*)$', l)
        if m and not l.lstrip().startswith('0000') and 'R_AARCH64' not in l:
            fn[cur].append([int(m.group(1), 16), m.group(2), None])
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+(R_AARCH64_\w+)\s+(\S+)$', l)
        if m and fn[cur]:
            fn[cur][-1][2] = (m.group(1), m.group(2))
    for name, insl in fn.items():
        byaddr = {a: k for k, (a, _, _) in enumerate(insl)}
        regs = {}
        def val(r): return regs.get(r)
        def run(k, regs, depth=0):
            # evaluate a log call at instruction index k (bl) with regs
            f = regs.get('x1'); fm = regs.get('x0'); ln = regs.get('w2')
            if isinstance(f, tuple) and isinstance(fm, tuple) and isinstance(ln, int):
                res[(ro.get(f[1]), ro.get(fm[1]))].add(ln)
        for k, (addr, txt, rel) in enumerate(insl):
            t = txt.split('\t') if '\t' in txt else txt.split(None, 1)
            op = t[0].strip()
            args = t[1] if len(t) > 1 else ''
            args = re.sub(r'\s*//.*$', '', args)
            a = [x.strip() for x in args.split(',')]
            if op == 'adrp' and rel:
                regs[a[0]] = ('page', rel[1])
            elif op == 'add' and rel and rel[0].startswith('R_AARCH64_ADD_ABS'):
                m = re.match(r'\.rodata\+0x([0-9a-f]+)', rel[1])
                if m:
                    regs[a[0]] = ('ro', int(m.group(1), 16))
                    regs[a[0].replace('x', 'w')] = None
                    # normalise as x reg tuple
                    regs[a[0]] = ('ro', int(m.group(1), 16))
                else:
                    regs.pop(a[0], None)
            elif op == 'mov' and a[0][0] in 'wx' and a[1].startswith('#'):
                try: v = int(a[1][1:], 0)
                except Exception: v = None
                regs[a[0]] = v
                if a[0][0] == 'x': regs['w' + a[0][1:]] = v
            elif op == 'mov' and a[0][0] in 'wx' and a[1][0] in 'wx':
                regs[a[0]] = regs.get(a[1])
                if a[0][0] == 'x': regs['w' + a[0][1:]] = regs.get(a[1])
            elif op == 'bl' and rel and re.search(calls, rel[1]):
                # normalise: w2 line
                v = dict(regs)
                x0, x1 = v.get('x0'), v.get('x1')
                w2 = v.get('w2', v.get('x2'))
                if x0 and x1 and x0[0] == 'ro' and x1[0] == 'ro' and isinstance(w2, int):
                    res[(ro.get(x1[1]), ro.get(x0[1]))].add(w2)
                regs = {kk: vv for kk, vv in regs.items() if kk[1:] not in ('0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '10', '11', '12', '13', '14', '15', '16', '17') or False}
            elif op == 'b' and not rel:
                # unconditional branch: if target is a log call site, evaluate with current regs
                m = re.match(r'0x([0-9a-f]+)', a[0])
                if m:
                    tgt = int(m.group(1), 16)
                    kk = byaddr.get(tgt)
                    for j in range(kk, min(kk + 4, len(insl))) if kk is not None else []:
                        if insl[j][2] and re.search(calls, insl[j][2][1]) and insl[j][1].startswith('bl'):
                            x0, x1 = regs.get('x0'), regs.get('x1')
                            w2 = regs.get('w2')
                            if x0 and x1 and x0[0] == 'ro' and x1[0] == 'ro' and isinstance(w2, int):
                                res[(ro.get(x1[1]), ro.get(x0[1]))].add(w2)
                            break
                regs = dict(regs)
            elif op in ('bl', 'blr'):
                # call clobbers caller-saved regs
                for r in list(regs):
                    if re.match(r'[wx](\d|1[0-7])$', r): regs.pop(r)
            else:
                # any other instruction writing a register: invalidate dest (first arg) if it is a register we track
                if a and re.match(r'[wx]\d+$', a[0]) and op not in ('cmp', 'cmn', 'tst', 'str', 'strb', 'strh', 'stur', 'stp', 'tbz', 'tbnz', 'cbz', 'cbnz', 'b'):
                    regs.pop(a[0], None)
                    regs.pop(('w' if a[0][0] == 'x' else 'x') + a[0][1:], None)
    return res

def c_unescape(s):
    return ast.literal_eval('"' + s + '"') if True else s

def find_funcs(lines):
    """return list of (start_line, end_line, name) for top-level function definitions (brace at col 0)"""
    out = []
    depth = 0
    name = None
    start = None
    for i, l in enumerate(lines):
        if depth == 0:
            m = re.match(r'^(?:static\s+|noinline\s+|inline\s+|__attribute__\(\(.*?\)\)\s+)*[\w\s\*]+?\b(\w+)\s*\(', l)
            if m and not l.startswith(('#', ' ', '\t')) and '=' not in l.split('(')[0] and not l.rstrip().endswith(';'):
                name = m.group(1); start = i
        depth += l.count('{') - l.count('}')
        if depth == 0 and start is not None and '}' in l:
            out.append((start, i, name)); start = None
    return out

def parse_stmts(lines, logfn, prefix):
    fl = find_funcs(lines)
    stmts = []
    i = 0
    i = 0
    tag = None
    while i < len(lines):
        l = lines[i]
        mt = re.match(r'^[ 	]*#[ 	]*define[ 	]+MCA_LOG_TAG[ 	]+"([^"]*)"', l)
        if mt: tag = mt.group(1)
        m = re.search(r'\b' + logfn + r'\s*\(', l)
        if m and not l.lstrip().startswith(('#define', '//', '*', '/*')):
            j = i
            while True:
                txt = '\n'.join(lines[i:j + 1])
                s = txt[txt.index(m.group(0)):]
                paren = s.count('(') - s.count(')')
                if paren <= 0 and (s.rstrip().endswith(';') or s.rstrip().endswith(')')):
                    break
                j += 1
                if j >= len(lines):
                    break
            s = txt[txt.index(m.group(0)) + len(m.group(0)):]
            pos = 0
            fmt = ''
            for mm in re.finditer(r'\s*"((?:[^"\\]|\\.)*)"', s):
                if mm.start() != pos:
                    break
                fmt += mm.group(1)
                pos = mm.end()
            try:
                fmts = (('[%s]%%s:%%d ' % tag) if prefix == 'AUTO' and tag else prefix) + ast.literal_eval('"' + fmt + '"')
            except Exception:
                fmts = None
            fname = None
            for (st, en, nm) in fl:
                if st <= i <= en:
                    fname = nm
            stmts.append((i, j, (fname, fmts)))
            i = j + 1
            continue
        i += 1
    return stmts


def main():
    a = sys.argv[1:]
    logfn = r'mca_log_(err|info|debug)'
    calls = r'__mca_log_(err|info|debug)'
    if '--logfn' in a:
        k = a.index('--logfn')
        logfn = a[k + 1]
        del a[k:k + 2]
    if '--calls' in a:
        k = a.index('--calls')
        calls = a[k + 1]
        del a[k:k + 2]
    dump, prefix, srcs = a[0], a[1], a[2:]
    stock = load_stock(dump, calls)
    files = []
    crlf_of = {}
    occ = collections.defaultdict(list)    # key -> [(file idx, stmt idx)]
    for fi, src in enumerate(srcs):
        raw = open(src, encoding='utf-8', errors='replace').read().split('\n')
        lines = [l for l in raw if not re.match(r'^\s*#line\s+\d+\s*$', l)]
        stmts = parse_stmts(lines, logfn, prefix)
        files.append((lines, stmts))
        for si, (i, j, key) in enumerate(stmts):
            occ[key].append((fi, si))
    pins = {}      # (fi, si) -> N (closing line number)
    missing = []
    ambiguous = []
    for key, lst in occ.items():
        if key not in stock:
            missing.append(key)
            continue
        ls = sorted(stock[key])
        if len(ls) == len(lst):
            for (fi, si), n in zip(lst, ls):
                pins[(fi, si)] = n
        elif len(ls) == 1:
            for fs in lst:
                pins[fs] = ls[0]
        else:
            ambiguous.append((key, ls, len(lst)))
    for fi, src in enumerate(srcs):
        lines, stmts = files[fi]
        ins = {}
        for si, (i, j, key) in enumerate(stmts):
            if (fi, si) in pins:
                ins[i] = '#line %d' % (pins[(fi, si)] - (j - i + 1) + 1)
        out = []
        for i, l in enumerate(lines):
            if i in ins:
                out.append(ins[i])
            out.append(l)
        open(src, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
    used = set(occ)
    unused = [k for k in stock if k not in used]
    print('pinned', len(pins), 'stmts; no stock counterpart', len(missing), '; ambiguous', len(ambiguous), '; stock keys without source', len(unused))
    for k in missing:
        print('  NOSTOCK (source->none):', k)
    for k, v, n in ambiguous:
        print('  AMBIG', k, 'stock lines', v, 'source stmts', n)
    for k in unused:
        print('  UNUSED stock:', k, sorted(stock[k]))


if __name__ == '__main__':
    main()
