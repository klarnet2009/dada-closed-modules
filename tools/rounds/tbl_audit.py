#!/usr/bin/env python3
"""tbl_audit.py stock.ko ours.ko : data-object content audit (g22f).
1. section sizes per data section name (stock vs ours)
2. every named data object (sized symbol in a data/rodata/bss-class section): per normalised name multiset of (class,size,content+reloc-target hash); prints ALL differing names with a word diff
3. interest classes (regdomain/channel/pcl/opclass/roam/ini/...) : count, bytes, equal/different
4. anon chunks stock-only / built-only, full detail incl. size and reloc targets"""
import sys, re, collections, bisect, difflib
sys.path.insert(0, "<work-dir>")
from compare_ko_v2 import Elf, norm, FIELD
S, O = Elf(sys.argv[1]), Elf(sys.argv[2])

print("== 1. section sizes (data-like)")
def secsizes(e):
    d = collections.Counter()
    for i, s in enumerate(e.secs):
        if e.kind[i] in ("data", "bss", "str") or s.name.startswith(".rodata"):
            n = s.name
            m = re.match(r"^(\.init|\.exit)?\.(bss|data|rodata)(\.\.ro_after_init|\.\.read_mostly|\.str\d+\.\d+|\.cst\d+|\.rel\.ro|\.rel\.local)?", n)
            d[(m.group(0) if m else n) + ("*" if m and n != m.group(0) else "")] += s.size
    return d
a, b = secsizes(S), secsizes(O)
tot_a = tot_b = 0
for k in sorted(set(a) | set(b)):
    flag = "" if a[k] == b[k] else "  DIFF %+d" % (b[k] - a[k])
    print("  %-34s stock %8d ours %8d%s" % (k, a[k], b[k], flag))

print("== 2. named objects")
oa, ob = S.objects(), O.objects()
print("  names stock %d ours %d, objects stock %d ours %d" % (len(oa), len(ob), sum(map(len, oa.values())), sum(map(len, ob.values()))))
print("  only stock:", sorted(set(oa) - set(ob))[:20], " only ours:", sorted(set(ob) - set(oa))[:20])
bad = [k for k in oa if k in ob and collections.Counter(oa[k]) != collections.Counter(ob[k])]
print("  differing names (multiset of class,size,content):", len(bad))
for k in bad:
    print("   ", k, "stock", oa[k], "ours", ob[k])

def dump(e, name, w=4):
    res = []
    for sh, lst in e.osyms.items():
        for s in lst:
            if norm(s.name) != name:
                continue
            sec = e.secs[sh]
            data = e.sdata(sh)[s.val:s.val + s.size]
            ro, rl = e.reloff.get(sh, []), e.rel.get(sh, [])
            rel = {}
            for j in range(bisect.bisect_left(ro, s.val), bisect.bisect_left(ro, s.val + s.size)):
                off, t, sy, add = rl[j]
                rel[off - s.val] = (FIELD.get(t, 8), e.tgtp(sy, add)[:90])
            out, o = [], 0
            while o < len(data):
                if o in rel:
                    n, t = rel[o]
                    out.append("+%04x R %s" % (o, t)); o += n; continue
                out.append("+%04x %s" % (o, data[o:o + w].hex())); o += w
            res.append((s.name, sec.name, s.size, out))
    return res
for k in bad:
    da, db = dump(S, k), dump(O, k)
    print("  ---", k, "stock:", [(n, sn, sz) for n, sn, sz, _ in da], "ours:", [(n, sn, sz) for n, sn, sz, _ in db])
    la = [x for _, _, _, o in da for x in o]; lb = [x for _, _, _, o in db for x in o]
    dl = list(difflib.unified_diff(la, lb, "stock", "ours", n=1, lineterm=""))
    print("    hunks:", sum(1 for l in dl if l.startswith("@@")), "changed lines:", sum(1 for l in dl if l[:1] in "+-" and l[:3] not in ("+++", "---")))
    for l in dl[:12]:
        print("    ", l)

print("== 3. interest classes")
CL = collections.OrderedDict([
    ("opclass/chan-list", r"opcls|op_class|opclass|chan(nel)?_?(list|tbl|table|map|freq)|channel|freq_|_freq|chlist|ch_list"),
    ("regdomain/country", r"reg|country|cc_|cntry|alpha2|domain|dfs|rd_|regdb"),
    ("pcl/policy_mgr", r"pcl|policy|pm_|concurr|sbs|dbs|valid_chan"),
    ("power/txpower", r"power|tpc|txpwr|pwr|eirp|psd|sar"),
    ("roam/11k/v/r", r"roam|neighbor|rrm|btm|11k|11v|11r|bss_trans|ft_|mdie|ese|wnm|rssi|threshold"),
    ("cfg/ini default", r"cfg_|_cfg|ini|default|dflt|cds_cfg|hdd_ctx_cfg"),
    ("rate/mcs/nss", r"mcs|rate|nss|rssi_mcs|he_|vht|ht_cap"),
])
names = sorted(oa)
tot = collections.Counter()
for cl, rx in CL.items():
    r = re.compile(rx, re.I)
    sel = [n for n in names if r.search(n)]
    eq = [n for n in sel if n in ob and collections.Counter(oa[n]) == collections.Counter(ob[n])]
    nb = sum(sz for n in sel for (_c, sz, _h) in oa[n])
    print("  %-20s %4d names (%6d bytes): equal %d, differ %d, missing %d" % (cl, len(sel), nb, len(eq), len(sel) - len(eq) - len([n for n in sel if n not in ob]), len([n for n in sel if n not in ob])))
    print("     differ:", [n for n in sel if n in ob and n not in eq][:20])
# size-ranked tables (non-string, >= 64 bytes) all equal?
big = [(sz, n) for n in names for (c, sz, h) in oa[n] if sz >= 64 and c.startswith(".rodata")]
print("  .rodata objects >=64 B: %d names, %d bytes, differing: %s" % (len(big), sum(s for s, _ in big), [n for s, n in big if n in bad]))
allro = [(sz, n) for n in names for (c, sz, h) in oa[n]]
print("  ALL named objects: %d bytes total; differing names carry %d bytes (stock)" % (sum(s for s, _ in allro), sum(sz for n in bad for (_c, sz, _h) in oa[n])))

print("== 4. anon chunks")
for e in (S, O):
    for si_, lst in e.rel.items():
        if e.kind[si_] != "meta":
            for off, t, sy, add_ in lst:
                e.tgt(sy, add_)
oa_, ob_ = collections.Counter(S.anon.values()), collections.Counter(O.anon.values())
def info(e, h):
    secs = [(sh, ad) for (sh, ad), v in e.anon.items() if v == h]
    out = []
    for sh, ad in secs[:2]:
        sec = e.secs[sh]
        hi = e.next_boundary(sh, ad)
        data = e.d[sec.off + ad:sec.off + hi]
        ro, rl = e.reloff.get(sh, []), e.rel.get(sh, [])
        rr = [(rl[j][0] - ad, e.tgtp(rl[j][2], rl[j][3])[:60]) for j in range(bisect.bisect_left(ro, ad), bisect.bisect_left(ro, hi))]
        out.append("%s+%#x len %d (trail0 stripped %d) head %s rel %s" % (sec.name, ad, hi - ad, len(data.rstrip(b"\0")), data[:24].hex(), rr[:3]))
    return out
for h in (oa_ - ob_).elements():
    print("  stock-only", h, info(S, h))
for h in (ob_ - oa_).elements():
    print("  ours-only ", h, info(O, h))
