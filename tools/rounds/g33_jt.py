import sys, re, collections, struct
sys.path.insert(0, "<work-dir>")
from compare_ko_v2 import Elf, norm
def jt(e):
    c = collections.Counter()
    for i in e.secidx.get("__jump_table", []):
        rel = {r[0]: r for r in e.rel.get(i, [])}
        for b in range(0, e.secs[i].size, 16):
            fn = re.sub(r"\+\d+$", "", e.tgtp(rel[b][2], rel[b][3])) if b in rel else "?"
            k = rel.get(b + 8)
            key = (e.tgtp(k[2], k[3]).split("+")[0], k[3] & 3) if k else "?"
            c[(fn, key)] += 1
    return c
def bug(e):
    c = collections.Counter()
    for (f, line), n in e.bug_lines().items():
        c[(re.sub(r"\+\d+$", "", f), line)] += n
    return c
for line in open("<work-dir>"):
    m, st, o = line.split()
    if m not in sys.argv[1:]: continue
    S, O = Elf("<work-dir>" + st), Elf("<work-dir>" + o)
    for nm, f in (("jump_table", jt), ("bug_table", bug)):
        a, b = f(S), f(O)
        print("%-18s %-10s stock %3d ours %3d  stock-only %s  ours-only %s" % (m, nm, sum(a.values()), sum(b.values()), list((a - b).items())[:4], list((b - a).items())[:4]))
