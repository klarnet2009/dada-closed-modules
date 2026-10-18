#!/usr/bin/env python3
"""unity.py <srcdir> <outdir> <stock.ko> : unity-TU copy of a module for the ThinLTO emulation (server side).
Every *.c of srcdir goes to outdir/tu/, outdir/<mod>_u.c includes them all; functions that are LOCAL in the stock get `static`
(prototype AND definition, first declaration wins); static helpers defined in several units are renamed per unit.
Original sources stay untouched. env SKIP="names" keeps those functions non-static."""
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
cs = sorted(x for x in os.listdir(src) if x.endswith('.c') and x not in os.environ.get('NOTU', '').split())
pat = {n: re.compile(r'^(?!static|\s|#|/)([A-Za-z_][\w \t\*]*?[\s\*])(%s)\(' % re.escape(n)) for n in loc}
done = set()
UNITS = {'matt_main.c': ['matt_tx.h', 'matt_sock.h', 'matt_init.h'], 'matt_rcv.c': ['matt_sack.h'], 'matt_bbr.c': ['matt_bbr_main.h'],
         'matt_addr.c': ['matt_inet4.h', 'matt_inet6.h']}
text = {fn: open(src + '/' + fn, encoding='utf8', errors='surrogateescape').read() for fn in os.listdir(src) if fn != 'STATUS.md'}
hdr_of = {h: c for c, hs in UNITS.items() for h in hs}
unit_of = lambda fn: hdr_of.get(fn, fn)
defs = {}
for fn, t in text.items():
    if fn in ('matt.h', 'matt_decl.h'):
        continue
    for l in t.split(chr(10)):
        m = re.match(r'^static\b[^;=]*?\b(\w+)\(', l)
        if m and not l.rstrip().endswith(';'):
            defs.setdefault(m.group(1), set()).add(unit_of(fn))
for n, us in defs.items():
    if len(us) > 1:
        for u in sorted(us)[1:]:
            for fn in list(text):
                if unit_of(fn) == u:
                    text[fn] = re.sub(r'\b%s\b' % n, '%s_%s' % (n, u.replace('.c', '').replace('.', '_')), text[fn])
        print('renamed duplicate static', n, sorted(us))
if 'matt_main.c' in text:
    text['matt_main.c'] = text['matt_main.c'].replace('ret = matt_sendmsg_locked(sk, msg);',
        'ret = ((int (*)(struct sock *, struct msghdr *))matt_sendmsg_locked)(sk, msg);')
    text['matt_main.c'] = text['matt_main.c'].replace('extern int matt_sendmsg_locked(struct sock *sk, struct msghdr *msg);', 'extern int matt_sendmsg_locked(struct sock *sk, struct msghdr *msg, size_t size);')
for fn in list(text):
    dst = out + ('/tu/' if fn.endswith('.c') else '/') + fn
    lines = text[fn].split(chr(10))
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
    open(dst, 'w', encoding='utf8', errors='surrogateescape').write(chr(10).join(lines))
mod = os.path.basename(src.rstrip('/'))
with open('%s/%s_u.c' % (out, mod), 'w') as f:
    f.write('/* unity TU */\n#define sk_stream_moderate_sndbuf hdr_sk_stream_moderate_sndbuf\n#include "matt.h"\n#undef sk_stream_moderate_sndbuf\n')
    for c in cs:
        f.write('#include "tu/%s"\n' % c)
print('static added for %d of %d local functions; missing: %s' % (len(done), len(loc), ' '.join(sorted(loc - done))))
