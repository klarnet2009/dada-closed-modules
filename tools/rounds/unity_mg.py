#!/usr/bin/env python3
"""unity_mg.py <srcdir> <outdir> <stock.ko> : unity-TU copy of mi_game for the ThinLTO emulation (server side). Variant of unity.py (matt).
Every *.c of srcdir goes to outdir/tu/, outdir/<mod>_u.c includes them in stock TU order; functions that are LOCAL in the stock get `static`
(prototype AND definition); static helpers defined in several units are renamed per unit. env SKIP="names" keeps those non-static.
RENVAR="a:b c:d" renames file-static variables per unit listed (name:unit.c). Original sources stay untouched."""
import os, re, shutil, subprocess, sys
src, out, stock = sys.argv[1:4]
skip = set(os.environ.get('SKIP', '').split())
B = '<build-dir>'
loc = set()
for l in subprocess.check_output([B, '-s', '-W', stock], text=True).splitlines():
    f = l.split()
    if len(f) >= 8 and f[3] == 'FUNC' and f[4] == 'LOCAL':
        loc.add(re.sub(r'\.llvm\.\d+$', '', f[7]))
loc -= skip
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out + '/tu')
ORDER = ['params.c', 'mi_game_load.c', 'mi_game_ca.c', 'flt.c']
cs = [c for c in ORDER if os.path.exists(src + '/' + c)]
UNITS = {'params.c': ['migt_a.h', 'migt_b.h', 'migt_c.h', 'migt_e.h', 'migt_f.h', 'migt_g.h', 'migt_h.h']}
text = {fn: open(src + '/' + fn, encoding='utf8', errors='surrogateescape').read() for fn in os.listdir(src) if fn not in ('STATUS.md',) and not fn.endswith('.bazel')}
hdr_of = {h: c for c, hs in UNITS.items() for h in hs}
unit_of = lambda fn: hdr_of.get(fn, fn)
pat = {n: re.compile(r'^(?!static|\s|#|/)([A-Za-z_][\w \t\*]*?[\s\*])(%s)\(' % re.escape(n)) for n in loc}
done = set()
defs = {}
for fn, t in text.items():
    if fn in ('mi_game.h', 'trace_mi_game.h'):
        continue
    for l in t.split('\n'):
        m = re.match(r'^static\b[^;=]*?\b(\w+)\(', l)
        if m and not l.rstrip().endswith(';'):
            defs.setdefault(m.group(1), set()).add(unit_of(fn))
for n, us in defs.items():
    if len(us) > 1:
        for u in sorted(us)[1:]:
            for fn in list(text):
                if unit_of(fn) == u:
                    text[fn] = re.sub(r'\b%s\b' % n, '%s_%s' % (n, u.replace('.c', '').replace('.', '_')), text[fn])
        print('renamed duplicate static function', n, sorted(us))
# auto-detect colliding file-level identifiers (struct/union/enum tags, variables) between units; rename in later units
tagpat = re.compile(r'^(?:struct|union|enum)\s+(\w+)\s*\{')
varpat = re.compile(r'^(?:static\s+)?(?:const\s+)?(?:(?:struct\s+|union\s+|enum\s+)?[\w]+[\s\*]+)+\**(\w+)(?:\[[^\]]*\])*\s*(?:=|;|__attribute__|__used|__read_mostly)')
pcpat = re.compile(r'^(?:static\s+)?DEFINE_PER_CPU\w*\([^,]+,\s*(\w+)')
idu = {}
for fn, t in text.items():
    if fn in ('mi_game.h', 'trace_mi_game.h'):
        continue
    for l in t.split(chr(10)):
        for pt in (tagpat, varpat, pcpat):
            m = pt.match(re.sub(r's*(__used|__read_mostly|__maybe_unused|__initdata|__ro_after_init)', '', l))
            if m and not m.group(1).startswith('__') and not l.startswith(('extern', 'typedef', 'return', 'goto')):
                idu.setdefault(m.group(1), set()).add(unit_of(fn))
for n, us in idu.items():
    if len(us) > 1 and n not in defs:
        for u in sorted(us, key=lambda x: ORDER.index(x))[1:]:
            os.environ['RENVAR'] = os.environ.get('RENVAR', '') + ' %s:%s' % (n, u)
        print('auto rename identifier', n, sorted(us))
for ent in os.environ.get('RENVAR', '').split():
    n, u = ent.split(':')
    for fn in list(text):
        if unit_of(fn) == u:
            text[fn] = re.sub(r'\b%s\b' % n, '%s_%s' % (n, u.replace('.c', '')), text[fn])
for fn in list(text):
    dst = out + ('/tu/' if fn.endswith('.c') else '/') + fn
    lines = text[fn].split('\n')
    for i, l in enumerate(lines):
        if not l or l[0] in ' \t#/{}*':
            continue
        for n in loc:
            if n in l:
                m = pat[n].match(l)
                if m and 'asm' not in l:
                    lines[i] = ('static ' + l[7:]) if l.startswith('extern ') else ('static ' + l)
                    done.add(n)
                    break
    open(dst, 'w', encoding='utf8', errors='surrogateescape').write('\n'.join(lines))
mod = os.path.basename(src.rstrip('/'))
with open('%s/%s_u.c' % (out, mod), 'w') as f:
    f.write('/* unity TU */\n#define CREATE_TRACE_POINTS\n')
    for c in cs:
        f.write('#include "tu/%s"\n' % c)
print('static added for %d of %d local functions; missing: %s' % (len(done), len(loc), ' '.join(sorted(loc - done))))
