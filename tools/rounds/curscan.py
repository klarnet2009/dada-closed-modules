#!/usr/bin/env python3
"""G40a: instruction-level scan of `current->field` accesses in arm64 .ko files.

curscan.py <stock.btf> <ours.btf> <out.json> <objdump> <ko>...
Linear scan of `llvm-objdump -d -r --no-show-raw-insn` per function: a register loaded by
`mrs xN, SP_EL0` (= current task_struct*) is tracked through add/mov; every memory operand
[xN, #imm] on it is recorded as an access to task_struct+off. Calls clobber x0-x18.
Each offset is resolved to a member path in the stock and in our BTF; paths that differ
(or hit android_kabi_reserved*) are printed.
shortcut: no control-flow analysis (linear order only); good enough for compiler-emitted current-> loads.
"""
import json, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from btfparse import BTF

MEM = re.compile(r'\[(x\d+|sp)(?:, #(-?0x[0-9a-f]+|-?\d+))?\](!?)')
NOWRITE = ('str', 'stp', 'stur', 'stlr', 'stxr', 'stlxr', 'cmp', 'cmn', 'tst', 'b', 'bl', 'blr', 'br', 'ret',
           'cbz', 'cbnz', 'tbz', 'tbnz', 'prfm', 'st', 'dmb', 'dsb', 'isb', 'hint', 'nop', 'bti', 'paciasp',
           'autiasp', 'msr', 'ccmp', 'ccmn', 'fcmp', 'brk', 'udf', 'hvc', 'smc', 'svc')


def num(s):
    return int(s, 0)


def member_at(b, t, off, depth=0):
    """byte offset -> member path inside struct t (descends into embedded structs/arrays)."""
    t = b.strip(t)
    k = b.types[t][0]
    if k == 'array' and depth < 12:
        et, _, n = b.types[t][5]
        es = b.sizeof(et) or 1
        i = off // es
        return '[%d]%s' % (i, member_at(b, et, off - i * es, depth + 1)) if i < n else '[?+%d]' % off
    if k not in ('struct', 'union') or depth > 12:
        return '+%d' % off if off else ''
    hits = []
    for name, mt, bo, bs in b.members(t):
        sz = (bs + 7) // 8 if bs else b.sizeof(mt)
        if bo // 8 <= off < bo // 8 + max(sz, 1):
            hits.append(name + ('' if not name else '.') + member_at(b, mt, off - bo // 8, depth + 1)
                        if b.types[b.strip(mt)][0] in ('struct', 'union', 'array') else (name or '?') + ('+%d' % (off - bo // 8) if off - bo // 8 else ''))
    return '|'.join(h.rstrip('.') for h in hits) if hits else '<hole+%d>' % off


def scan(objdump, ko):
    out = subprocess.run([objdump, '-d', '-r', '--no-show-raw-insn', ko], capture_output=True, text=True).stdout
    acc, cur, fn = [], {}, None
    for line in out.splitlines():
        if line.endswith('>:'):
            fn, cur = line.split('<', 1)[1][:-2], {}
            continue
        parts = line.split('\t')
        if len(parts) < 2 or ':' not in parts[0]:
            continue
        mn = parts[-2].strip() if len(parts) >= 3 else parts[-1].strip()
        ops = parts[-1].strip() if len(parts) >= 3 else ''
        if not mn or mn.startswith('R_AARCH64'):
            continue
        regs = re.findall(r'\b([xw])(\d+)\b', ops.split('[')[0])
        if mn == 'mrs' and ops.upper().endswith('SP_EL0'):
            cur['x' + regs[0][1]] = 0
            continue
        m = MEM.search(ops)
        if m and m.group(1) in cur:
            o = cur[m.group(1)] + (num(m.group(2)) if m.group(2) else 0)
            acc.append((fn, mn, o))
        dst = ('x' + regs[0][1]) if regs else None
        if mn in ('add', 'sub') and dst:
            mm = re.match(r'[xw]\d+, (x\d+), #(0x[0-9a-f]+|\d+)(?:, lsl #12)?', ops)
            if mm and mm.group(1) in cur:
                v = num(mm.group(2)) << (12 if 'lsl #12' in ops else 0)
                cur[dst] = cur[mm.group(1)] + (v if mn == 'add' else -v)
                continue
        if mn == 'mov' and dst:
            mm = re.match(r'x\d+, (x\d+)$', ops)
            if mm and mm.group(1) in cur:
                cur[dst] = cur[mm.group(1)]
                continue
        if mn in ('bl', 'blr'):
            for r in range(19):
                cur.pop('x%d' % r, None)
            continue
        if dst and not any(mn == p or (p in ('st',) and mn.startswith('st')) for p in NOWRITE):
            cur.pop(dst, None)
            if mn.startswith('ldp') and len(regs) > 1:
                cur.pop('x' + regs[1][1], None)
        if m and m.group(3) == '!' and m.group(1) in cur:
            cur.pop(m.group(1), None)
    return acc


def main():
    bs, bo = BTF.load(sys.argv[1]), BTF.load(sys.argv[2])
    ts, to = bs.structs()['struct task_struct'][0], bo.structs()['struct task_struct'][0]
    size = bs.types[ts][2]
    res, offs = {}, {}
    for ko in sys.argv[5:]:
        a = scan(sys.argv[4], ko)
        res[os.path.basename(ko)] = a
        for fn, mn, o in a:
            offs.setdefault(o, set()).add(os.path.basename(ko))
    table = []
    for o in sorted(offs):
        ps = member_at(bs, ts, o) if 0 <= o < size else '<outside %d>' % o
        po = member_at(bo, to, o) if 0 <= o < size else '<outside %d>' % o
        table.append({'off': o, 'stock': ps, 'ours': po, 'nmod': len(offs[o]), 'mods': sorted(offs[o])})
    json.dump({'per_module': res, 'table': table}, open(sys.argv[3], 'w'), indent=0)
    print('modules %d, modules with current-> accesses %d, distinct offsets %d, accesses %d' % (
        len(res), sum(1 for v in res.values() if v), len(offs), sum(len(v) for v in res.values())))
    for r in table:
        flag = 'DIFF' if r['stock'] != r['ours'] else ('RSV' if 'kabi_reserved' in r['stock'] or '<' in r['stock'] else '')
        print('%-4s 0x%04x %3d  stock=%-45s ours=%-45s %s' % (flag, r['off'] & 0xffffffff, r['nmod'], r['stock'][:45], r['ours'][:45],
                                                      ' '.join(r['mods'][:6]) if flag else ''))


if __name__ == '__main__':
    main()
