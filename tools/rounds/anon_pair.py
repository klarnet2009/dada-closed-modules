#!/usr/bin/env python3
"""anon_pair.py stock.ko ours.ko : prove the stock-only / ours-only anonymous data chunks of compare_ko_v2 hold the same bytes
(token stream = chunk bytes with every relocation field replaced by a marker of its resolved target; ours chunk must be a substring
of a stock-only region, and the non-zero bytes of every stock-only region must be covered by ours chunks, and vice versa)."""
import sys, collections, bisect, hashlib
sys.path.insert(0, "<work-dir>")
from compare_ko_v2 import Elf, FIELD
S, O = Elf(sys.argv[1]), Elf(sys.argv[2])
for e in (S, O):
    for si_, lst in e.rel.items():
        if e.kind[si_] != "meta":
            for off, t, sy, add_ in lst:
                e.tgt(sy, add_)
def toks(e, sh, ad):
    sec = e.secs[sh]; hi = e.next_boundary(sh, ad)
    data = bytearray(e.d[sec.off + ad:sec.off + hi])
    ro, rl = e.reloff.get(sh, []), e.rel.get(sh, [])
    for j in range(bisect.bisect_left(ro, ad), bisect.bisect_left(ro, hi)):
        off, t, sy, add = rl[j]
        n = FIELD.get(t, 8)
        mk = b"\xfe" + hashlib.sha1(e.tgtp(sy, add).encode()).digest()[:n - 1] if n > 1 else b"\xfe"
        data[off - ad:off - ad + n] = mk[:n]
    return bytes(data)
ca, cb = collections.Counter(S.anon.values()), collections.Counter(O.anon.values())
sa = [(k, h) for k, h in S.anon.items() if (ca - cb)[h]]
sb = [(k, h) for k, h in O.anon.items() if (cb - ca)[h]]
print("stock-only %d  ours-only %d" % (len(sa), len(sb)))
A = [(k, toks(S, *k)) for k, h in sa]
B = [(k, toks(O, *k), O.secs[k[0]].name) for k, h in sb]
# ours -> stock
cov = {i: bytearray(len(t)) for i, (k, t) in enumerate(A)}
ok = True
for k, t, nm in B:
    t2 = t.rstrip(b"\0")
    if not t2:
        t2 = t
    hit = False
    for i, (ka, ta) in enumerate(A):
        p = ta.find(t2)
        while p >= 0:
            hit = True
            for q in range(p, p + len(t2)):
                cov[i][q] = 1
            p = ta.find(t2, p + 1)
    if not hit:
        ok = False
        print("  OURS chunk not found in any stock-only region:", nm, k, t2[:32].hex())
# stock uncovered nonzero
for i, (k, t) in enumerate(A):
    un = [q for q in range(len(t)) if t[q] and not cov[i][q]]
    if un:
        ok = False
        print("  STOCK region %s+%#x: %d non-zero bytes not covered by any ours-only chunk, first at +%d (%s)" % (S.secs[k[0]].name, k[1], len(un), un[0], t[un[0]:un[0] + 12].hex()))
print("RESULT:", "all ours-only chunks occur in stock-only regions and vice versa (same bytes, other placement)" if ok else "NOT PROVEN")
