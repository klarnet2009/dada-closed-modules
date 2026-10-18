import subprocess, sys, re
which, mod, sec, st, ln = sys.argv[1:6]
st = int(st, 16); ln = int(ln, 16)
p = '<work-dir>' % ('stock' if which == 'stock' else 'built', mod)
O = '<build-dir>'
out = subprocess.check_output([O + 'llvm-readelf', '--relocs', p], text=True)
cur = None
for l in out.split('\n'):
    m = re.match(r"Relocation section '\.rela(\S+)'", l)
    if m: cur = m.group(1); continue
    if cur and cur.startswith(sec):
        f = l.split()
        if len(f) >= 5 and re.match(r'^[0-9a-f]+$', f[0]):
            off = int(f[0], 16)
            if st <= off < st + ln:
                print('+0x%x %s %s %s' % (off - st, f[2], f[4], ' '.join(f[5:])))
