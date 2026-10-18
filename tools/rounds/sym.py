"""sym.py <module> <func>...: linear symbolic digest. g-offsets tracked through add/mov."""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
mod = sys.argv[1]
names = sys.argv[2:]
sys.argv = [sys.argv[0], mod]
import pp
W = {'ldr': None, 'ldrb': 1, 'ldrh': 2, 'ldrsw': 4, 'ldar': None, 'ldarb': 1, 'ldapr': None, 'ldaprb': 1, 'ldursw': 4, 'ldur': None, 'ldrsb': 1, 'str': None, 'strb': 1, 'strh': 2, 'stur': None, 'stlr': None, 'stlrb': 1}

def gref(v):
    return v if isinstance(v, str) else None

for name in names:
    lines = pp.show(name)
    regs = {}
    tgt = set()
    for l in lines:
        for m in re.finditer(r'<0x([0-9a-f]+)>', l):
            tgt.add(int(m.group(1), 16))
    print('=====', name)
    for l in lines:
        m = re.match(r'^([0-9a-f]+): (.*)$', l)
        off, t = int(m.group(1), 16), m.group(2)
        pre = 'L%x:' % off if off in tgt else ''
        def out(s):
            print('%s %x: %s' % (pre, off, s) if pre else '%x: %s' % (off, s))
        mm = re.match(r'^(x\d+) = &(.*)$', t)
        if mm:
            sy = mm.group(2)
            gm = re.match(r'^g\[0x([0-9a-f]+)\]$', sy)
            regs[mm.group(1)] = ('A', 'g', int(gm.group(1), 16)) if gm else ('S', sy)
            if pre: out('')
            continue
        mm = re.match(r'^(add|sub)\s+(x\d+), (x\d+), #0x([0-9a-f]+)$', t)
        if mm and regs.get(mm.group(3), ('?',))[0] == 'A':
            b = regs[mm.group(3)]
            d = int(mm.group(4), 16) * (1 if mm.group(1) == 'add' else -1)
            regs[mm.group(2)] = ('A', b[1], b[2] + d)
            if pre: out('')
            continue
        mm = re.match(r'^(ldr|ldrb|ldrh|ldrsw|ldar|ldarb|ldapr|ldaprb|ldursw|ldur|ldrsb)\s+([wx])(\d+), \[(g\[0x[0-9a-f]+\]|x\d+)(?:, #(-?0x[0-9a-f]+))?\]$', t)
        if mm:
            base, imm = mm.group(4), int(mm.group(5) or '0', 16)
            if base.startswith('g['):
                a = 'g+0x%x' % (int(base[4:-1], 16))
            elif regs.get(base, ('?',))[0] == 'A':
                a = 'g+0x%x' % (regs[base][2] + imm)
            elif regs.get(base, ('?',))[0] == 'S':
                a = '%s+0x%x' % (regs[base][1], imm)
            else:
                a = '%s%+d' % (base, imm) if imm else base
            w = W[mm.group(1)] or (4 if mm.group(2) == 'w' else 8)
            regs['x' + mm.group(3)] = ('V', '*(%d)%s' % (w, a))
            regs['w' + mm.group(3)] = regs['x' + mm.group(3)]
            if pre: out('')
            continue
        mm = re.match(r'^(str|strb|strh|stur|stlr|stlrb|stadd)\s+([wx]\w+), \[(g\[0x[0-9a-f]+\]|x\d+)(?:, #(-?0x[0-9a-f]+))?\]$', t)
        if mm:
            base, imm = mm.group(3), int(mm.group(4) or '0', 16)
            if base.startswith('g['):
                a = 'g+0x%x' % int(base[4:-1], 16)
            elif regs.get(base, ('?',))[0] == 'A':
                a = 'g+0x%x' % (regs[base][2] + imm)
            else:
                a = '%s+0x%x' % (base, imm) if imm else base
            v = regs.get(mm.group(2), ('?', mm.group(2)))
            out('STORE %s(%s) -> %s' % (mm.group(1), v[-1] if isinstance(v[-1], str) else mm.group(2), a))
            continue
        mm = re.match(r'^(mov|movk|add|sub|lsl|lsr|asr|and|orr|sxtw|ubfx|mul|neg|mvn|eor)\s+([wx]\d+), (.*)$', t)
        if mm:
            regs[mm.group(2)] = ('E', '(%s %s)' % (mm.group(1), mm.group(3)))
            if mm.group(2)[0] == 'w': regs['x' + mm.group(2)[1:]] = regs[mm.group(2)]
            if mm.group(1) == 'mov' and mm.group(3).startswith(('x','w')) and mm.group(3) in regs:
                regs[mm.group(2)] = regs[mm.group(3)]
            if pre: out('')
            continue
        mm = re.match(r'^bl (\S+)', t)
        if mm:
            def ex(i):
                v = regs.get('x%d' % i)
                if not v: return '?'
                if v[0] == 'A': return '&g+0x%x' % v[2]
                if v[0] == 'S': return '&' + v[1]
                return v[1]
            args = ', '.join('%s' % ex(i) for i in range(6))
            out('CALL %s(%s)' % (mm.group(1), args))
            regs = {k: v for k, v in regs.items() if re.match(r'^[xw](19|2[0-8])$', k)}
            regs['x0'] = ('V', 'ret')
            continue
        out(t)
