#!/usr/bin/env python3
"""anon_find.py stock.ko ours.ko : for every stock-only / ours-only anonymous chunk without relocations, find its bytes in the OTHER module (raw search in all rodata/data sections) and print where"""
import sys, collections, bisect
sys.path.insert(0, "<work-dir>")
from compare_ko_v2 import Elf
S, O = Elf(sys.argv[1]), Elf(sys.argv[2])
for e in (S, O):
    for si_, lst in e.rel.items():
        if e.kind[si_] != "meta":
            for off, t, sy, add_ in lst:
                e.tgt(sy, add_)
def secs(e):
    return [(i, s.name, e.d[s.off:s.off + s.size]) for i, s in enumerate(e.secs) if s.type != 8 and (s.name.startswith((".rodata", ".data")) or e.kind[i] in ("str", "data"))]
def region(e, sh, ad):
    hi = e.next_boundary(sh, ad)
    return e.d[e.secs[sh].off + ad:e.secs[sh].off + hi], hi
ca, cb = collections.Counter(S.anon.values()), collections.Counter(O.anon.values())
def run(src, dst, only, tag):
    dsecs = secs(dst)
    for (sh, ad), h in src.anon.items():
        if not only[h]: continue
        data, hi = region(src, sh, ad)
        ro, rl = src.reloff.get(sh, []), src.rel.get(sh, [])
        nrel = bisect.bisect_left(ro, hi) - bisect.bisect_left(ro, ad)
        if nrel: 
            print("%s %s+%#x len %d has %d relocs (skipped)" % (tag, src.secs[sh].name, ad, hi - ad, nrel)); continue
        key = data.rstrip(b"\0")
        hits = []
        for i, nm, d in dsecs:
            p = d.find(key)
            while p >= 0 and len(hits) < 4:
                hits.append("%s+%#x" % (nm, p)); p = d.find(key, p + 1)
        print("%s %s+%#x len %d (nz %d) head %s -> in other module: %s" % (tag, src.secs[sh].name, ad, hi - ad, len(key), key[:12].hex(), hits or "NOT FOUND"))
run(S, O, ca - cb, "stock-only")
run(O, S, cb - ca, "ours-only ")
