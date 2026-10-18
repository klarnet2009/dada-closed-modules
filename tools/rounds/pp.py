"""pp.py <module> <func> [func...] : compressed stock disassembly (strings resolved, adrp/add merged, bss as g[off]).
env GBASE (hex, default 0x10) = .bss offset of the main global struct; GNAME default g"""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cdis import parse

mod = sys.argv[1]
base = os.path.join(os.path.dirname(os.path.abspath(__file__)), mod)
funcs, order = parse(os.path.join(base, 'dis.txt'))
ro = {}
cur = None
for line in open(os.path.join(base, 'rodata.txt'), encoding='utf8', errors='replace'):
    m = re.match(r"^\.rodata\+0x([0-9a-f]+)\t(.*)$", line.rstrip('\n'))
    if m:
        try:
            s = eval(m.group(2))
        except Exception:
            s = m.group(2)
        ro[int(m.group(1), 16)] = s
rokeys = sorted(ro)
GB = int(os.environ.get('GBASE', '0x10'), 16)
GN = os.environ.get('GNAME', 'g')
GEND = int(os.environ.get('GEND', '0x400'), 16)


def rostr(off):
    if off in ro:
        s = ro[off]
    else:
        k = [x for x in rokeys if x <= off]
        if not k:
            return '.rodata+0x%x' % off
        b = k[-1]
        s = ro[b][off - b:] if off - b < len(ro[b]) else '?'
        return 'STR@%x+%d:%r' % (off, off - b, s[:60])
    return 'STR@%x:%r' % (off, s[:90])


_SM = {'.bss': {}, '.data': {}, '.text': {}}
_secidx = {}
try:
    for _l in open(os.path.join(base, 'sections.txt')):
        _m = re.match(r'^(\d+)\] (\.bss|\.data|\.text) ', _l)
        if _m:
            _secidx[_m.group(1)] = _m.group(2)
except Exception:
    pass
try:
    for _l in open(os.path.join(base, 'syms.txt')):
        _f = _l.split()
        if len(_f) >= 8 and _f[3] in ('OBJECT', 'FUNC') and _f[6] in _secidx and not _f[7].startswith('__param') and not _f[7].startswith('__UNIQUE'):
            _SM[_secidx[_f[6]]].setdefault(int(_f[1], 16), (int(_f[2]), _f[7]))
except Exception:
    pass


def symname(sec, o):
    m = _SM.get(sec)
    if not m:
        return None
    best = None
    for k in m:
        if k <= o < k + max(m[k][0], 1) and (best is None or k > best):
            best = k
    if best is None:
        return None
    n = m[best][1]
    return n if o == best else '%s+0x%x' % (n, o - best)


def sym(sy):
    m = re.match(r'^(\.bss|\.data|\.rodata|\.text)\+0x([0-9a-f]+)$', sy)
    if not m:
        m2 = re.match(r'^(\.bss|\.data|\.rodata)$', sy)
        if m2:
            m = (m2.group(1), '0')
        else:
            return sy
        sec, o = m[0], int(m[1], 16)
    else:
        sec, o = m.group(1), int(m.group(2), 16)
    if sec == '.rodata':
        return rostr(o)
    nm = symname(sec, o)
    if nm:
        return nm
    if sec == '.bss':
        if GB <= o < GEND:
            return '%s[0x%x]' % (GN, o - GB)
        return 'bss+0x%x' % o
    return 'data+0x%x' % o


def show(name):
    ins = funcs[name]
    start = ins[0][0]
    out = []
    pend = {}
    n = len(ins)
    for idx, (o, t) in enumerate(ins):
        t = re.sub(r'0x[0-9a-f]+ <([^>]*)>', lambda m: '<' + m.group(1).split('+')[-1] + '>' if '+' in m.group(1) else '<' + m.group(1) + '>', t)
        t = re.sub(r'\s+', ' ', t)
        rel = o - start
        if t.startswith(('paciasp', 'autiasp', 'mov x29, sp', '.word')):
            continue
        if re.match(r'^(stp|ldp) x\d+, x\d+, \[sp', t) or re.match(r'^(str|ldr) x\d+, \[sp, #0x[0-9a-f]+\]$', t) and False:
            continue
        m = re.match(r'^adrp (x\d+), <[^>]*> ;ADR_PREL_PG_HI21 (\S+)$', t)
        if m:
            pend[m.group(1)] = m.group(2)
            out.append('%x: %s = &%s' % (rel, m.group(1), sym(m.group(2))))
            continue
        m = re.match(r'^add (x\d+), (x\d+), #0x0 ;ADD_ABS_LO12_NC (\S+)$', t)
        if m:
            continue
        m = re.match(r'^(ldr|ldrb|ldrh|ldrsw|ldar|ldarb|ldapr|ldaprb|str|strb|strh|stlr|stp|ldp|stur|ldur|ldursw|ldrsb|ldrsh|stlrb|stadd|ldxr) (.*?) ;LDST\d+_ABS_LO12_NC (\S+)$', t)
        if m:
            t2 = re.sub(r'\[x\d+(, #0x[0-9a-f]+)?\]', '[' + sym(m.group(3)) + ']', m.group(2))
            out.append('%x: %s %s' % (rel, m.group(1), t2))
            continue
        m = re.match(r'^(.*) ;(CALL26|JUMP26) (\S+)$', t)
        if m:
            out.append('%x: %s %s' % (rel, m.group(1).split()[0], sym(m.group(3)) if m.group(3).startswith('.text') else m.group(3)))
            continue
        out.append('%x: %s' % (rel, t))
    return out


for name in sys.argv[2:]:
    if name not in funcs:
        print('NO', name)
        continue
    print('=====', name, len(funcs[name]))
    print('\n'.join(show(name)))
