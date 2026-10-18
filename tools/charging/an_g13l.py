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
    if mm and mm.group(1) in ('strategy_buckchg_proc_data', 'mca_smartchg_data', 'adapter_power_cap_info','mca_quick_charge_process_data','mca_cp_work_mode','mca_quick_charge_smartchg_data','mca_quick_charge_sysfs_data','mca_quick_charge_ibus_queue_info','mca_quick_charge_ibat_queue_info'):
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
for l in p.stdout.split('\n'):
    if not l.strip(): continue
    a = l.split(':', 1)[0].strip()
    try: av = int(a, 16)
    except: print(l); continue
    if av < frm or av > to: continue
    if re.match(r"\s*[0-9a-f]+: (paciasp|autiasp|ret)", l): continue
    ann = []
    for m in re.finditer(r"\[(x\d+|w\d+), #(0x[0-9a-f]+)\]", l):
        if m.group(1) in ('sp', 'x29'): continue
        o = int(m.group(2), 16)
        if o >= 0x20 or True:
            nm = look(o)
            if nm: ann.append('{%s}' % nm)
    for m in re.finditer(r"#(0x[0-9a-f]+)\b", l):
        pass
    l = rebase(re.sub(r"\s+", " ", l).strip())
    print(l + ('   ;' + ' '.join(ann) if ann else ''))
