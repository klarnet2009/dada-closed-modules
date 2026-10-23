"""remap_lines.py <src.c> <linepairs.json> : rewrite every '#line N' (CN stock __LINE__ pins) to the GLOBAL stock value.
Known CN lines (from __mca_log call pairs, CN stock vs global stock) map exactly; unknown lines use the delta of the nearest
lower known line (the deltas are piecewise constant: insertions of the 13 new functions) and are listed on stdout."""
import sys, json, re, bisect, collections
src, pj = sys.argv[1:3]
pairs = json.load(open(pj))
m = {}
for a, b in pairs:
    assert m.setdefault(a, b) == b, (a, b)
ks = sorted(m)
out = []
guess = []
n = 0
for ln in open(src, newline='').read().split('\n'):
    r = re.match(r'^#line (\d+)(\r?)$', ln)
    if r:
        n += 1
        v = int(r.group(1))
        if v in m:
            nv = m[v]
        else:
            i = bisect.bisect_right(ks, v) - 1
            d = (m[ks[i]] - ks[i]) if i >= 0 else 0
            nv = v + d
            guess.append((v, nv))
        ln = '#line %d%s' % (nv, r.group(2))
    out.append(ln)
open(src, 'w', newline='').write('\n'.join(out))
print('pins rewritten', n, 'guessed (not in stock log pairs)', len(guess))
for v, nv in guess:
    print('  guess', v, '->', nv)
