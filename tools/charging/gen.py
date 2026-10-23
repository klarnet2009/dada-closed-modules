import sys, re, os
sys.path.insert(0, "bin")
import casesig
from compare_ko_v2 import Elf
enum = {}
for m in re.finditer(r"(FG_IC_PROP_\w+)\s*=\s*(\d+)", open("tmp/platform_fg_ic_ops.h").read()):
    enum[m.group(1)] = int(m.group(2))
tbl = {}
for m in re.finditer(r"mca_sysfs_attr_(ro|rw)\(fg_sysfs,\s*(\d+),\s*(FG_IC_PROP_\w+),\s*(\w+)\)", open("tmp/bq27z561.c").read()):
    tbl[enum[m.group(3)]] = (m.group(4), m.group(2), m.group(1))
S, O = Elf("st/gl_bq27z561.ko"), Elf("built/bq27z561.ko")
os.environ["NENT"] = "124"
es, _, _, shs, _ = casesig.analyze(S, "stf/fg_sysfs_show.s")
eo, _, _, sho, _ = casesig.analyze(O, "ourf/fg_sysfs_show.s")
os.environ["NENT"] = "116"
es2, _, _, sts, _ = casesig.analyze(S, "stf/fg_sysfs_store.s")
eo2, _, _, sto, _ = casesig.analyze(O, "ourf/fg_sysfs_store.s")
def fmt(ev):
    out = []
    for e in sorted(ev):
        if e in ("imm:4096", "call:scnprintf") or e.startswith("cmp:0"): continue
        if e.startswith("str:[fuelgauge_ic]"): e = "log"
        if e.startswith("str:fg_") or e.startswith("imm:") and False: continue
        out.append(e.replace("call:", "").replace("str:", "fmt ").replace("\n", "\n").replace("|", "/"))
    return ", ".join(dict.fromkeys(out))
print("| id | name | mode ours (stock) | show (stock-equal? ) | store |")
print("|---|---|---|---|---|")
for i in range(0, 124):
    nm = tbl.get(i, ("-", "-", "-"))
    shS = shs[i] if i < len(shs) else set(); shO = sho[i] if i < len(sho) else set()
    sS = sts[i - 8] if 8 <= i and i - 8 < len(sts) else set(); sO = sto[i - 8] if 8 <= i and i - 8 < len(sto) else set()
    shs_eq = "=" if shS == shO else "DIFF"
    sts_eq = "=" if sS == sO else "DIFF"
    nm0 = nm[0] if i != 123 else "upgrade_fw(ABSENT)"
    print("| %d | %s | %s | %s %s | %s %s |" % (i, nm0, nm[1] + (" (stock 0664)" if i == 35 else ""), shs_eq, fmt(shS), sts_eq if 8 <= i else "n/a", fmt(sS) if 8 <= i else ""))
