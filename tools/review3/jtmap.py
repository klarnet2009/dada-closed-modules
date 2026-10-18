# jtmap.py ko func sec+off n width : for each jump-table entry print the target (adr base + 4*val) and the 3 register-masked insns there
import sys, re, struct
sys.path.insert(0, "<work-dir>")
from compare_ko_v2 import Elf
ko, fn, loc, n, w = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
e = Elf(ko); sn, off = loc.split("+"); off = int(off, 0)
sec = [s for s in e.secs if s.name == sn][0]; d = e.d[sec.off + off: sec.off + off + n * w]
vals = [struct.unpack_from({1: "<B", 2: "<h", 4: "<i"}[w], d, i * w)[0] for i in range(n)]
L = {}
for l in open(sys.argv[6]):
    m = re.match(r"\s*([0-9a-f]+): (.*)$", l.rstrip())
    if m: L[int(m.group(1), 16)] = re.sub(r"\b[wx]\d+\b", "r", m.group(2)[:40]).strip()
base = int(sys.argv[7], 16)
for i, v in enumerate(vals):
    t = base + (4 * v if w < 4 else v)
    print(i, " ; ".join(L.get(t + 4 * k, "?") for k in range(3)) if t is not None else v)
