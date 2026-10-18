"""sum.py: stdin = a.sh listing; collapse call windows: the instructions that load x0-x7/w0-w7 (adrp/mov/add/ldr/cset...) since the last
control-flow point are folded into one 'CALL fn(args)' line.  Stores of arg regs to the stack (str x2,[sp,...]) are dropped too."""
import sys, re
lines = [l.rstrip('\n') for l in sys.stdin if l.strip()]
tgt = set()
for l in lines:
    if re.match(r"[0-9a-f]+:? bl ", l): continue
    for m in re.finditer(r"\bL([0-9a-f]+)\b", l):
        tgt.add(int(m.group(1), 16))
def addr(l):
    m = re.match(r"([0-9a-f]+):? ", l)
    return int(m.group(1), 16) if m else None
CF = re.compile(r"^[0-9a-f]+:? (b|b\.\w+|cbz|cbnz|tbz|tbnz|bl|blr|br|ret|bti|csel|csinc|ccmp|cmp|tst|cmn)\b")
out = []; win = []; defs = {}
def flush():
    global win, defs
    out.extend(win); win = []; defs = {}
def argidx(r):
    m = re.match(r"[wx](\d+)$", r)
    return int(m.group(1)) if m else None
def pretty(rhs):
    m = re.search(r'"(.*)"', rhs)
    if m: return '"' + m.group(1).strip("'") + '"'
    return rhs
for l in lines:
    a = addr(l)
    if a is not None and a in tgt: flush()
    body = l.split(None, 1)[1] if ' ' in l else l
    # a call
    m = re.match(r"bl\s+\S+\s+(\S+)", body)
    if m and not body.startswith('bl L') or (m and body.split()[1].startswith('L') and len(body.split()) > 2):
        fn = body.split()[-1]
        args = []
        for i in range(0, 8):
            if i in defs: args.append('a%d=%s' % (i, defs[i][1]))
        used = set(id(defs[i][0]) for i in defs)
        win2 = [w for w in win if id(w) not in used]
        win2 = [w for w in win2 if not re.match(r"[0-9a-f]+:? (str|stur) [wx][2-7], \[sp", w)]
        out.extend(win2)
        out.append("%s CALL %s(%s)" % (l.split()[0], fn, ', '.join(args)))
        win = []; defs = {}
        continue
    if CF.match(l):
        flush(); out.append(l); continue
    mm = re.match(r"(?:\S+ )(mov|movk|add|sub|adrp|ldr|ldrb|ldrh|ldrsw|cset|orr|lsl|mvn|neg|asr|lsr)\s+([wx]\d+),\s*(.*?)(;.*)?$", l)
    win.append(l)
    if mm:
        op, rd, rest, ann = mm.groups(); i = argidx(rd)
        if i is not None and i < 8 and op != 'movk':
            txt = rest.strip()
            if op == 'adrp': txt = pretty(rest)
            elif op == 'add': txt = rest.replace(', #', '+')
            elif op == 'ldr' or op.startswith('ldr'): txt = rest + (ann or '')
            defs[i] = (l, txt if op != 'mov' else rest.strip())
            # keep the object identity of the window line
            defs[i] = (win[-1], defs[i][1])
        elif op == 'movk' and i is not None and i in defs:
            pass
flush()
print('\n'.join(out))
