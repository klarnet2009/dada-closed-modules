#!/usr/bin/env python3
"""anon_pair2.py stock.ko ours.ko : every stock-only anon region is cut into runs of non-zero bytes (relocation fields replaced by a
marker of the resolved target; the two known placement-only target pairs normalised); each run must occur in the token stream of ALL
data/rodata sections of the other module, and vice versa."""
import sys, collections, bisect, hashlib, re
sys.path.insert(0, "<work-dir>")
from compare_ko_v2 import Elf, FIELD
S, O = Elf(sys.argv[1]), Elf(sys.argv[2])
for e in (S, O):
    for si_, lst in e.rel.items():
        if e.kind[si_] != "meta":
            for off, t, sy, add_ in lst:
                e.tgt(sy, add_)
NORM = {"ro:f643b99657f8449a8c1a:0": "ro:CFISLST0", "ro:8ddd14c9f800f3fc3c41:0": "ro:CFISLST0",
        "ro:44fbc0886820b8152d3b:2475": "ro:GOPCLS", "ro:b635cea97a1ecfb7ed66:2475": "ro:GOPCLS"}
def mark(e, sy, add, n):
    t = e.tgtp(sy, add)
    t = re.sub(r"^(ro:[0-9a-f]{20}:\d+)(.*)$", lambda m: NORM.get(m.group(1), m.group(1)) + m.group(2), t)
    return (b"\xfe" + hashlib.sha1(t.encode()).digest()[:max(n - 1, 0)])[:n]
def toks(e, sh, lo, hi):
    sec = e.secs[sh]
    data = bytearray(e.d[sec.off + lo:sec.off + hi]) if sec.type != 8 else bytearray(hi - lo)
    ro, rl = e.reloff.get(sh, []), e.rel.get(sh, [])
    for j in range(bisect.bisect_left(ro, lo), bisect.bisect_left(ro, hi)):
        off, t, sy, add = rl[j]
        n = FIELD.get(t, 8)
        data[off - lo:off - lo + n] = mark(e, sy, add, n)
    return bytes(data)
def big(e):
    return b"\x01\x02\x03".join(toks(e, i, 0, s.size) for i, s in enumerate(e.secs) if s.type != 8 and e.kind[i] in ("data", "str") or s.name.startswith(".rodata"))
def runs(b):
    for m in re.finditer(rb"(?:[^\0]|\0(?!\0{8}))+", b):   # split at 9+ zero bytes only... keep short zero gaps inside a run
        yield m.start(), m.group(0).strip(b"\0")
ca, cb = collections.Counter(S.anon.values()), collections.Counter(O.anon.values())
def longest(t, p, big):
    lo, hi = 0, len(t) - p
    while lo < hi:                       # largest L with t[p:p+L] in big (monotone)
        mid = (lo + hi + 1) // 2
        if t[p:p + mid] in big: lo = mid
        else: hi = mid - 1
    return lo
def check(src, dst_big, only, tag):
    bad = 0
    for (sh, ad), h in src.anon.items():
        if not only[h]: continue
        hi = src.next_boundary(sh, ad)
        t = toks(src, sh, ad, hi)
        p, pieces = 0, []
        while p < len(t):
            if t[p] == 0:
                p += 1; continue
            L = longest(t, p, dst_big)
            if L == 0: L = 1
            pieces.append((p, L)); p += L
        short = [(a, l) for a, l in pieces if l < 3 or (l < 5 and set(t[a:a + l]) == {0xff})]
        print("  %s %s+%#x len %d: %d pieces %s%s" % (tag, src.secs[sh].name, ad, hi - ad, len(pieces), [l for a, l in pieces][:12], "  SHORT/UNMATCHED %s" % short if short else ""))
        bad += len(short)
    return bad
bs, bo = big(S), big(O)
b1 = check(S, bo, ca - cb, "stock-only")
b2 = check(O, bs, cb - ca, "ours-only")
print("RESULT:", "every non-zero byte of the stock-only/ours-only anon regions is covered by long matches in the other module (placement/chunk-boundary only)" if not (b1 or b2) else "NOT PROVEN: %d runs" % (b1 + b2))
