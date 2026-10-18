"""an.py <dumpdir> <structname> <func> [from_hex] [to_hex]: compact stock listing (fn.py style) with every [xN, #off] annotated by
the member of <structname> (module-local BTF, btf_all.txt) at that offset (nested struct members of known blocks are flattened).
Registers sp/x29 are not annotated.  Usage: python3 an.py dump_mca_strategy_buckchg strategy_buckchg_dev strategy_buckchg_monitor_workfunc"""
import sys, re, subprocess, os
d, sname, fn = sys.argv[1:4]
frm = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
to = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0xfffff
btf = open(d + '/btf_all.txt', encoding='utf-8', errors='replace').read().split('\n')
def block(name):
    out = []; on = False
    for l in btf:
        if l.startswith('struct %s {' % name): on = True; continue
        if on:
            if l.startswith('}'): break
            out.append(l)
    return out
def fields(name, base=0, prefix=''):
    res = []
    for l in block(name):
        m = re.match(r"\s*(?:struct |union |enum )?(.+?)\s+(\*?\??\w+)(\[\d+\])?\s*(?::\s*\d+)?;\s*/\* \+(\d+)", l)
        if not m: continue
        ty, nm, arr, off = m.groups(); off = int(off) + base
        res.append((off, prefix + nm.lstrip('*') + (arr or ''), ty))
    return res
F = fields(sname)
# flatten known nested blocks
ext = []
for off, nm, ty in F:
    t = ty.strip()
    mm = re.match(r"(?:struct )?(\w+)$", t)
    if mm and mm.group(1) in ('strategy_buckchg_proc_data', 'mca_smartchg_data', 'adapter_power_cap_info'):
        ext += fields(mm.group(1), off, nm + '.')
F = sorted(F + ext)
def look(o):
    best = None
    for off, nm, ty in F:
        if off <= o and (best is None or off >= best[0]): best = (off, nm, ty)
    if best is None: return None
    return best[1] + ('' if best[0] == o else '+%d' % (o - best[0]))
p = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'fn.py'), d, fn], capture_output=True, text=True, encoding='utf-8', errors='replace')
base = 0
for sl in open(d + '/syms.txt'):
    sp = sl.split()
    if len(sp) >= 8 and sp[7] == fn and sp[3] == 'FUNC': base = int(sp[1], 16)
def rebase(l):
    m = re.match(r"(\w+: \S+ (?:[wx]\d+, |#0x[0-9a-f]+, )*)(0x[0-9a-f]+)(.*)$", l)
    if m and re.match(r"\w+: (b|b\.\w+|cbz|cbnz|tbz|tbnz|bl) ", l):
        t = int(m.group(2), 16)
        if base and base <= t < base + 0x10000:
            return m.group(1) + 'L%x' % (t - base) + m.group(3)
    return l
TRK = dict((a.split(':')[0], int(a.split(':')[1])) for a in os.environ.get('REGBASE', '').split(',') if ':' in a)
WR = re.compile(r"\w+: (mov|movk|movz|adrp|add|adds|sub|subs|ldr|ldrb|ldrh|ldrsw|ldrsb|ldrsh|ldur|ldurb|ldurh|ldursw|orr|and|eor|csel|csinc|csinv|csneg|cset|csetm|lsl|lsr|asr|mul|madd|msub|smull|smulh|umull|umulh|mvn|neg|uxtw|sxtw|sxtb|sxth|uxtb|uxth|ubfx|sbfx|ubfiz|bfi|bfxil|udiv|sdiv) ([wx]\d+)")
def reg64(r): return 'x' + r[1:]
for l in p.stdout.split('\n'):
    if not l.strip(): continue
    a = l.split(':', 1)[0].strip()
    try: av = int(a, 16)
    except: print(l); continue
    if av < frm or av > to: continue
    if re.match(r"\s*[0-9a-f]+: (paciasp|autiasp|ret)", l): continue
    ls = re.sub(r"\s+", " ", l).strip()
    ann = []
    if TRK:
        for m in re.finditer(r"\[(x\d+|w\d+)(?:, #(-?0x[0-9a-f]+))?\]", ls):
            r = reg64(m.group(1))
            if r in TRK:
                o = int(m.group(2), 16) if m.group(2) else 0
                nm = look(o + TRK[r])
                if nm: ann.append('{%s}' % nm)
        m = re.match(r"\w+: (sub|add) (x\d+), (x\d+), #(0x[0-9a-f]+)$", ls)
        mv = re.match(r"\w+: mov (x\d+), (x\d+)$", ls)
        if m and reg64(m.group(3)) in TRK:
            k = int(m.group(4), 16)
            v = TRK[m.group(3)] + (k if m.group(1) == 'add' else -k)
            TRK[m.group(2)] = v
            nm = look(v)
            if nm: ann.append('&%s' % nm)
        elif mv and mv.group(2) in TRK:
            TRK[mv.group(1)] = TRK[mv.group(2)]
        else:
            mw = WR.match(ls)
            if mw: TRK.pop(reg64(mw.group(2)), None)
            mp = re.match(r"\w+: ldp ([wx]\d+), ([wx]\d+)", ls)
            if mp: TRK.pop(reg64(mp.group(1)), None); TRK.pop(reg64(mp.group(2)), None)
            if re.match(r"\w+: bl ", ls):
                for k in list(TRK):
                    if int(k[1:]) <= 18: del TRK[k]
        print(rebase(ls) + ('   ;' + ' '.join(ann) if ann else ''))
        continue
    for m in re.finditer(r"\[(x\d+|w\d+), #(0x[0-9a-f]+)\]", l):
        if m.group(1) in ('sp', 'x29'): continue
        o = int(m.group(2), 16)
        nm = look(o)
        if nm: ann.append('{%s}' % nm)
    l = rebase(ls)
    print(l + ('   ;' + ' '.join(ann) if ann else ''))
