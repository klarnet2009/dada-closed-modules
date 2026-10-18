"""lst2.py <obj.ko|.o> <func> : compact listing (function-relative branch targets @off, reloc targets / strings resolved, adrp+add folded).
Works on the stock .ko and on our LTO object (any string section). Run on the server (needs llvm-objdump)."""
import re, struct, subprocess, sys

B = '<build-dir>'
obj, fn = sys.argv[1], sys.argv[2]
d = open(obj, 'rb').read()
shoff, = struct.unpack_from('<Q', d, 0x28)
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3a)
secs = []
for i in range(shnum):
    nm, ty, fl, ad, of, sz = struct.unpack_from('<IIQQQQ', d, shoff + i * shentsize)
    secs.append([nm, of, sz, ty])
so = secs[shstrndx][1]
def cstr(o):
    e = d.index(b'\0', o)
    return d[o:e].decode('latin1')
byname = {}
for s in secs:
    byname[cstr(so + s[0])] = s
def resolve(t):
    m = re.match(r'(\.[\w.]+)\+0x([0-9a-f]+)$', t)
    if m and m.group(1) in byname and ('rodata' in m.group(1)) and byname[m.group(1)][3] != 8:
        s = byname[m.group(1)]
        o = s[1] + int(m.group(2), 16)
        e = d.find(b'\0', o)
        txt = d[o:e]
        if 0 < len(txt) < 200 and all(32 <= c < 127 or c in (9, 10) for c in txt):
            return '"' + txt.decode('latin1')[:90].replace('\n', '\\n') + '"'
    return t
out = subprocess.run([B + 'llvm-objdump', '-d', '-r', '--no-show-raw-insn', '--disassemble-symbols=' + fn, obj],
                     capture_output=True, text=True).stdout.split('\n')
rows = []
base = None
for l in out:
    m = re.match(r'([0-9a-f]+) <(.+)>:', l)
    if m:
        if m.group(2) == fn:
            base = int(m.group(1), 16)
        continue
    r = re.match(r'\s+[0-9a-f]+:\s+(R_AARCH64_\w+)\s+(\S+)', l)
    if r and rows:
        ty, t = r.groups()
        t = resolve(t)
        if 'ADD_ABS_LO12' in ty and rows[-1][1].startswith('add') and len(rows) > 1 and rows[-2][1].startswith('adrp'):
            rows.pop()
            continue
        rows[-1][2] = t
        continue
    m = re.match(r'\s+([0-9a-f]+):\s+(\S.*)$', l)
    if m and base is not None:
        a, t = m.groups()
        t = re.sub(r'\s*//.*$', '', t).replace('\t', ' ')
        t = re.sub(r'0x[0-9a-f]+ <%s\+0x([0-9a-f]+)>' % re.escape(fn), lambda mm: '@%x' % int(mm.group(1), 16), t)
        t = re.sub(r'0x[0-9a-f]+ <%s>' % re.escape(fn), '@0', t)
        t = re.sub(r' <[^>]*>', '', t)
        rows.append([int(a, 16) - base, t, ''])
for a, t, x in rows:
    if t.startswith('bl ') and x:
        t = 'bl'
    elif t.startswith('adrp') and x:
        t = re.sub(r', 0x[0-9a-f]+$', '', t)
    print('%4x: %-40s %s' % (a, t[:60], x[:100]))
