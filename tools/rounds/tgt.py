"""tgt.py <ko> <func> <regex of reloc text>: for each reloc matching, find block start and the branches into it with the nearest preceding bl callee."""
import re, subprocess, sys
B = '<build-dir>'
ko, fn, rx = sys.argv[1:4]
src = open('<work-dir>').read().replace(chr(10)+'main()'+chr(10), chr(10))
exec(src)
SD = secdata(ko); ST = symtab(ko)
txt = subprocess.check_output([B, '-dr', '--no-show-raw-insn', ko], text=True, errors='replace').split(chr(10))
ins, cur, rel = [], False, {}
for l in txt:
    m = re.match(r'^[0-9a-f]{16} <([^>]+)>:', l)
    if m:
        cur = re.sub(r'\.llvm\.\d+$', '', m.group(1)) == fn
        continue
    if not cur:
        continue
    m = re.match(r'^\s+([0-9a-f]+):\s+R_AARCH64_\w+\s+(\S+)', l)
    if m and ins:
        rel[ins[-1][0]] = resolve(m.group(2), SD, ST); continue
    m = re.match(r'^\s+([0-9a-f]+):\s+(.*)$', l)
    if m:
        ins.append((int(m.group(1), 16), re.sub(r'\s+', ' ', m.group(2)).strip()))
sd = None
addr = [a for a, _ in ins]
for a, t in ins:
    if a in rel and re.search(rx, rel[a]):
        i = addr.index(a)
        j = i
        while j > 0 and not re.match(r'^(b|ret|brk|br)\b', ins[j - 1][1]):
            j -= 1
        start = ins[j][0]
        print('reloc', hex(a), rel[a], 'blockstart', hex(start))
        for k, (aa, tt) in enumerate(ins):
            m = re.search(r'\b0x%x\b' % start, tt)
            if m and re.match(r'^(b|b\.\w+|cbz|cbnz|tbz|tbnz)\b', tt):
                prev = [(a2, t2, rel.get(a2)) for a2, t2 in ins[max(0, k - 12):k] if t2.startswith('bl ')]
                print('   from', hex(aa), tt[:40], 'prev bl:', prev[-1][2] if prev else None)
