"""gen_trace.py <stock.ko> <dis.txt> <out.h> [--sys NAME] [--incl hdr.h] [--ovr overrides.py]
Generate the TRACE_EVENT header of a stock module (OS4 Xiaomi modules) from the stock binary alone:
  - events, field types/names/order: `trace_event_fields_<evt>` table + relocated strings (tdump.py)
  - TP_printk: `print_fmt_<evt>` ("fmt", REC->a, ...) -> __entry->
  - TP_PROTO: BTF proto of `__traceiter_<evt>` (types resolved through TYPEMAP, task_struct pointer = the only struct)
  - TP_fast_assign: symbolic execution of `trace_event_raw_event_<evt>` (register copy / load-from-arg / constant / `p ? p->x : c`)
Anything the tracer cannot express is emitted as `/* TODO */` and can be replaced through the overrides file
(dict  OVR[evt] = dict(proto=[(type,name),..], assign=[(field, 'C expr'),..]) ).
Runs on the build server (needs python3, compare_ko.Elf, btf.py)."""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tdump, tproto

# BTF base-type id -> C type (local split BTF, base ids unresolvable offline; verified by kcfi/bytes on xr_qs/xr_qi)
TYPEMAP = {'T8': 'unsigned int', 'T10': 'unsigned long', 'T11': 'int', 'T16': 'void *', 'T18': 'u32', 'T20': 'bool',
           'T27': 'u64', 'T322': 'pid_t', 'struct ?str7525 *': 'struct task_struct *'}
TASK_MEMBERS = {0x618: 'pid'}          # task_struct member offsets known for our 6.6 GKI (pid)

SZ = {'b': 1, 'h': 2, 'w': 4, 'x': 8}


def parse_dis(path):
    fn = None
    ins = {}
    for l in open(path, errors='replace'):
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            fn = m.group(1); ins[fn] = []; continue
        if fn is None:
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(.*?)\s*$', l)
        if m and 'R_AARCH64' not in l:
            ins[fn].append((int(m.group(1), 16), re.sub(r'\s*//.*$', '', m.group(2)).strip()))
            continue
        m = re.search(r'R_AARCH64_\w+\s+(\S+)', l)
        if m:
            ins[fn].append((-1, 'RELOC ' + m.group(1)))
    return ins


def regnum(r):
    r = r.strip()
    if r in ('xzr', 'wzr'):
        return 'z'
    if r in ('sp', 'wsp'):
        return 'sp'
    return r[1:] if r[0] in 'xw' else r


def trace_assign(code, nargs):
    """symbolic run of trace_event_raw_event_<evt>: returns list of (entry_offset, size, expr) in store order, or None on failure"""
    R = {}
    for i in range(nargs):
        R[str(i)] = ('arg', i)
    entry = None
    stores = []
    labels = {}
    for idx, (a, t) in enumerate(code):
        if a >= 0:
            labels[a] = idx
    in_body = False
    idx = 0
    n = len(code)

    def val(r):
        k = regnum(r)
        return ('const', 0) if k == 'z' else R.get(k, ('unk', r))

    def run(idx, stop_idx, R, stores):
        while idx < stop_idx:
            a, t = code[idx]
            idx += 1
            if t.startswith('RELOC'):
                prev = code[idx - 2][1] if idx >= 2 else ''
                tg = t.split()[1]
                if tg == 'trace_event_buffer_reserve':
                    R['0'] = ('entry',)
                    R['__inbody'] = True
                elif tg == 'trace_event_buffer_commit':
                    return idx, 'commit'
                continue
            if not R.get('__inbody'):
                # prologue: only register copies of arguments matter
                m = re.match(r'mov\s+([xw]\d+),\s*([xw]\d+)$', t)
                if m:
                    R[regnum(m.group(1))] = val(m.group(2))
                continue
            mnem, _, ops = t.partition('\t')
            ops = ops.strip()
            if mnem == 'mov' or mnem == 'orr' and 'wzr' in ops:
                m = re.match(r'([xw]\d+),\s*(.+)$', ops)
                if m:
                    d, s = m.groups()
                    if s.startswith('#'):
                        R[regnum(d)] = ('const', int(s[1:].split()[0], 0))
                    elif re.match(r'[xw]\d+$|[xw]zr$', s):
                        R[regnum(d)] = val(s)
                    else:
                        R[regnum(d)] = ('unk', t)
            elif mnem in ('ldr', 'ldur', 'ldrb', 'ldrh', 'ldrsw', 'ldrsb', 'ldrsh'):
                m = re.match(r'([xw]\d+),\s*\[([xw]?\w+)(?:,\s*#(-?0x[0-9a-f]+|-?\d+))?\]$', ops)
                if m:
                    d, b, off = m.groups()
                    R[regnum(d)] = ('load', val(b), int(off, 0) if off else 0, mnem)
                else:
                    R[regnum(ops.split(',')[0])] = ('unk', t)
            elif mnem in ('str', 'stur', 'strb', 'strh'):
                m = re.match(r'([xw]\w+),\s*\[([xw]\w+)(?:,\s*#(-?0x[0-9a-f]+|-?\d+))?\]$', ops)
                if m:
                    s, b, off = m.groups()
                    if val(b) == ('entry',):
                        sz = {'strb': 1, 'strh': 2}.get(mnem, 8 if s[0] == 'x' else 4)
                        stores.append((int(off, 0) if off else 0, sz, val(s)))
            elif mnem == 'stp':
                m = re.match(r'([xw]\w+),\s*([xw]\w+),\s*\[([xw]\w+)(?:,\s*#(-?0x[0-9a-f]+|-?\d+))?\]$', ops)
                if m:
                    s1, s2, b, off = m.groups()
                    if val(b) == ('entry',):
                        sz = 8 if s1[0] == 'x' else 4
                        o = int(off, 0) if off else 0
                        stores.append((o, sz, val(s1))); stores.append((o + sz, sz, val(s2)))
            elif mnem in ('cbz', 'cbnz'):
                # ternary diamond:  cbz xN, Lelse ; <then> ; b Lend ; Lelse: <else> ; Lend:
                m = re.match(r'(\w+),\s*0x([0-9a-f]+)', ops)
                reg, tgt = m.group(1), int(m.group(2), 16)
                cond = val(reg)
                if cond == ('entry',):
                    continue
                R1, S1 = dict(R), []
                # find the `b Lend` in the fall-through part
                j = idx
                while j < n and not (code[j][1].startswith('b\t') and code[j][0] >= 0):
                    if code[j][0] >= tgt:
                        break
                    j += 1
                if j < n and code[j][1].startswith('b\t') and code[j][0] < tgt:
                    end = int(code[j][1].split('0x')[1].split()[0], 16)
                    _, st = run(idx, j, R1, S1)
                    R2, S2 = dict(R), []
                    _, st = run(labels[tgt], labels[end], R2, S2)
                    for k in set(R1) | set(R2):
                        if R1.get(k) != R2.get(k):
                            R[k] = ('tern', cond, R1.get(k), R2.get(k)) if mnem == 'cbz' else ('tern', cond, R2.get(k), R1.get(k))
                        else:
                            R[k] = R1[k]
                    # stores of both branches: pair by offset
                    d1 = {s[0]: s for s in S1}; d2 = {s[0]: s for s in S2}
                    for o in sorted(set(d1) | set(d2), key=lambda o: min(stores_pos(S1, o), stores_pos(S2, o))):
                        if o in d1 and o in d2:
                            stores.append((o, d1[o][1], ('tern', cond, d1[o][2], d2[o][2]) if mnem == 'cbz' else ('tern', cond, d2[o][2], d1[o][2])))
                        else:
                            stores.append((o, (d1.get(o) or d2.get(o))[1], ('unk', 'one-sided store')))
                    idx = labels[end]
                else:
                    # reserve-failure check (`cbz x0, Lskip`): continue linearly
                    pass
            elif mnem in ('and', 'cset', 'sxtw', 'sxtb', 'uxtb', 'ubfx', 'cmp', 'cinc', 'tst', 'add', 'sub', 'lsr', 'asr', 'lsl', 'csel'):
                m = re.match(r'([xw]\d+),', ops)
                if m and mnem not in ('cmp', 'tst'):
                    mm = re.match(r'and\s+([xw]\d+),\s*([xw]\d+),\s*#0x1$', t)
                    R[regnum(m.group(1))] = ('bool', val(mm.group(2))) if mm else ('unk', t)
        return idx, 'end'

    S = []
    run(0, n, R, S)
    return S


def stores_pos(S, o):
    for i, s in enumerate(S):
        if s[0] == o:
            return i
    return 1 << 30


def expr_c(e, names, ptrs):
    k = e[0]
    if k == 'arg':
        return names[e[1]]
    if k == 'const':
        return str(e[1] - (1 << 32) if e[1] >= (1 << 31) and e[1] < (1 << 32) else e[1])
    if k == 'bool':
        return expr_c(e[1], names, ptrs)
    if k == 'load':
        b = e[1]
        if b[0] == 'arg':
            mem = TASK_MEMBERS.get(e[2])
            if mem:
                return f'{names[b[1]]}->{mem}'
            return f'/* TODO {names[b[1]]}+{e[2]:#x} */ 0'
    if k == 'tern':
        return f'{expr_c(e[1], names, ptrs)} ? {expr_c(e[2], names, ptrs)} : {expr_c(e[3], names, ptrs)}'
    return f'/* TODO {e} */ 0'


def main():
    ko, dis, out = sys.argv[1:4]
    opt = dict(zip(sys.argv[4::2], sys.argv[5::2]))
    ovr = {}
    if '--ovr' in opt:
        exec(open(opt['--ovr']).read(), {'OVR': ovr}) if False else None
        ns = {}
        exec(open(opt['--ovr']).read(), ns)
        ovr = ns.get('OVR', {})
    e = tdump.Elf(ko)
    ins = parse_dis(dis)
    pr = tproto.protos(ko)
    evs = tdump.events(e)
    sysname = opt.get('--sys')
    if not sysname:
        for s in e.syms:
            if s['name'].startswith('str__') and s['name'].endswith('__trace_system_name'):
                sysname = s['name'][5:-len('__trace_system_name')]
    incl = opt.get('--incl')
    base = os.path.splitext(os.path.basename(out))[0]
    guard = '_' + base.upper() + '_H'
    L = ['/* SPDX-License-Identifier: GPL-2.0 */',
         f'/* generated by g1e_tools/gen_trace.py from the OS4 stock {os.path.basename(ko)} */',
         '', '#undef TRACE_SYSTEM', f'#define TRACE_SYSTEM {sysname}', '',
         f'#if !defined({guard}) || defined(TRACE_HEADER_MULTI_READ)', f'#define {guard}', '',
         '#include <linux/tracepoint.h>']
    if incl:
        L.append(f'#include "{incl}"')
    L.append('')
    for ev in evs:
        flds = tdump.fields(e, ev)
        fmt = tdump.print_fmt(e, ev).replace('REC->', '__entry->')
        params = [(n, TYPEMAP.get(t, t)) for n, t in pr[ev]][1:]      # drop `void *__data`
        # parameter names: take them from the field the argument is stored into
        nargs = len(params) + 1
        code = ins['trace_event_raw_event_' + ev]
        st = trace_assign(code, nargs)
        # field offsets
        off = 8
        foff = []
        for f in flds:
            off = (off + f['align'] - 1) // f['align'] * f['align']
            foff.append(off); off += f['size']
        names = {}
        assign = []
        ptr_idx = [i + 1 for i, (_, t) in enumerate(params) if t.endswith('*') and 'task_struct' in t]
        for i, (_, t) in enumerate(params):
            names[i + 1] = 'task' if (i + 1) in ptr_idx and len(ptr_idx) == 1 else (f'task{ptr_idx.index(i + 1) + 1}' if (i + 1) in ptr_idx else f'a{i + 1}')
        # name plain args after the field they feed
        for o, sz, ex in st or []:
            if ex[0] in ('arg',) or (ex[0] == 'bool' and ex[1][0] == 'arg'):
                a = ex[1] if ex[0] == 'arg' else ex[1][1]
                fi = [k for k, fo in enumerate(foff) if fo == o]
                if fi and a not in ptr_idx:
                    names[a] = flds[fi[0]]['name']
        # unique parameter names
        seen = set()
        for k in sorted(names):
            while names[k] in seen:
                names[k] += '_'
            seen.add(names[k])
        stmts = []
        if opt.get('--order', 'field') == 'field':
            st = sorted(st or [], key=lambda x: x[0])
        for o, sz, ex in st or []:
            fi = [k for k, fo in enumerate(foff) if fo == o]
            if not fi:
                stmts.append(('?', f'/* TODO store @{o:#x} {ex} */'))
                continue
            f = flds[fi[0]]
            if f['len']:
                stmts.append((f['name'], None)); continue
            stmts.append((f['name'], expr_c(ex, names, ptr_idx)))
        o = ovr.get(ev, {})
        if 'proto' in o:
            params = o['proto']
            names = {i + 1: n for i, (t, n) in enumerate(params)}
            params = [(n, t) for t, n in params]
        else:
            params = [(names[i + 1], t) for i, (_, t) in enumerate(params)]
        if 'assign' in o:
            stmts = o['assign']
        L.append(f'TRACE_EVENT({ev},')
        L.append('')
        L.append('\tTP_PROTO(' + ', '.join(f'{t} {n}' if not t.endswith('*') else f'{t}{n}' for n, t in params) + '),')
        L.append('')
        L.append('\tTP_ARGS(' + ', '.join(n for n, _ in params) + '),')
        L.append('')
        L.append('\tTP_STRUCT__entry(')
        for f in flds:
            if f['len']:
                L.append(f"\t\t__array({f['type'].split('[')[0]},\t{f['name']},\t{f['len']})")
            else:
                L.append(f"\t\t__field({f['type']},\t{f['name']})")
        L.append('\t),')
        L.append('')
        L.append('\tTP_fast_assign(')
        for fname, ex in stmts:
            if fname is None:
                L.append(f'		{ex}')
            elif ex is None:
                L.append(f'\t\t/* TODO array {fname} */')
            else:
                L.append(f'\t\t__entry->{fname} = {ex};')
        L.append('\t),')
        L.append('')
        L.append('\tTP_printk(' + fmt + ')')
        L.append(');')
        L.append('')
    L += [f'#endif /* {guard} */', '', '#undef TRACE_INCLUDE_PATH', '#define TRACE_INCLUDE_PATH .', '',
          '#undef TRACE_INCLUDE_FILE', f'#define TRACE_INCLUDE_FILE {base}', '', '#include <trace/define_trace.h>', '']
    open(out, 'w').write('\n'.join(L))
    print('events', len(evs), '->', out)


if __name__ == '__main__':
    main()
