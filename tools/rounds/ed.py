#!/usr/bin/env python3
"""ed.py <file> <fn-regex> <varfile.json>: json = list of {"find":..., "alts":[...]} groups; runs qq (python variant search, sequential, local) and prints diff-line count of matching function per combo."""
import sys, json, itertools, subprocess, os, re
f, fn, gj = sys.argv[1:4]
src = open(f, encoding='utf8').read()
groups = json.load(open(gj, encoding='utf8'))
mod = os.path.basename(os.path.dirname(os.path.abspath(f)))
res = []
try:
    for c in itertools.product(*[range(len(g['alts'])) for g in groups]):
        s = src
        for g, ci in zip(groups, c):
            assert s.count(g['find']) == 1, 'not unique: ' + g['find'][:50]
            s = s.replace(g['find'], g['alts'][ci])
        open(f, 'w', encoding='utf8').write(s)
        out = subprocess.run([os.environ.get("GITBASH", "bash"), "g11_tools/q.sh", mod], capture_output=True, text=True, env=dict(os.environ, FD=fn, HEADN="400")).stdout
        if 'error:' in out:
            res.append((10**9, c, [l for l in out.splitlines() if 'error' in l][:1])); continue
        tot = 0; info = ''
        for l in out.splitlines():
            m = re.match(r'=+ (\S+) (\d+) (\d+) diff lines (\d+)', l) or re.match(r'(\S+)\s+stock\s+(\d+)\s+built\s+(\d+)\s+diff\s+(\d+)', l)
            if m and re.search(fn, m.group(1)): tot += int(m.group(4)); info += ' %s:%s/%s' % (m.group(1)[:12], m.group(2), m.group(3))
        res.append((tot, c, info))
        print(tot, c, info, flush=True)
finally:
    open(f, 'w', encoding='utf8').write(src)
res.sort(key=lambda r: r[0])
print('BEST', res[:5])
