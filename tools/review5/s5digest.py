#!/usr/bin/env python3
"""s5digest.py (goal-G25a): one line per non-CLEAN function of symcmp5 `all` reports.
Usage: S5REP=/dir/with/all_<module>.txt python3 s5digest.py <module> ...   (report files are named all_<module>.txt)
Columns: verdict, same-atoms count, polarity kinds, and the facts that exist on ONE side only after reducing every fact to its kind
(call target / store base object / atomic / trap), i.e. value-form noise (G{..}, PHI, address forms, ldp/ldr) is ignored.
`S-only: - O-only: -` = both builds have the same set of effect kinds: strong CODEGEN hint, still not proof."""
import sys, re, collections, os

REP = os.environ.get('S5REP', '.')


def kind(l):
    l = l.strip()
    l = re.sub(r'^[-+] [SO]@[0-9a-f]+ ', '', l).lstrip('<')
    m = re.match(r'(call|st|stkarg|atomic|ret|trap|insn|msr)\(([^;)]*);?([^;]*)', l)
    if not m:
        return l[:30]
    k, a, b = m.groups()
    if k == 'st':
        return 'st:' + re.sub(r'\+.*|idx\(.*|[(,].*', '', b)
    if k == 'call':
        return 'call:?' if a.startswith('bl?') else 'call:' + a + (':' + b[:40] if a == 'ext:_printk' else '')
    if k == 'stkarg':
        return 'stkarg:' + a
    if k == 'atomic':
        return 'atomic:' + a + ':' + re.sub(r'\+.*|[(,].*', '', b)
    if k == 'ret':
        return 'ret'
    return k + ':' + a


for m in sys.argv[1:]:
    for b in open(os.path.join(REP, 'all_%s.txt' % m), errors='replace').read().split('\n=== '):
        L = b.split('\n')
        fn = L[0].split(':')[0].replace('=== ', '')
        s = [l for l in L if 'SUMMARY5' in l]
        if not s:
            continue
        if 'INCONCLUSIVE' in s[0] or 'ERROR' in s[0]:
            print(m, fn, s[0].split('SUMMARY5')[-1].strip()[:40])
            continue
        if 'CLEAN-POL' in s[0]:
            continue
        sa = re.search(r'same-atoms (\d+)', s[0])
        sa = int(sa.group(1)) if sa else 0
        S = collections.Counter()
        O = collections.Counter()
        for l in L:
            mm = re.match(r'\s+([-+]) ([SO])@', l)
            if mm:
                (S if mm.group(1) == '-' else O)[kind(l)] += 1
        pk = collections.Counter(re.match(r'\s*POLARITY-DIFF(?:-ATOMS)? \[(\w+)\]', l).group(1)
                                 for l in L if re.match(r'\s*POLARITY-DIFF(?:-ATOMS)? \[', l))
        print(m, fn, s[0].split('=>')[-1].strip()[:12], 'same=%d' % sa, dict(pk) or '', 'S-only:', dict(S - O) or '-', 'O-only:', dict(O - S) or '-')
