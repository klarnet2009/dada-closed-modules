import sys, re, difflib
sys.path.insert(0, "bin")
import casesig
from compare_ko_v2 import Elf
S, O = Elf("st/gl_bq27z561.ko"), Elf("built/g24a_bq27z561.ko")
def seq(elf, path):
    ins = casesig.parse(path); out = []; last = None
    for a, mn, ops, rels in ins:
        for r in rels:
            e = casesig.reloc_ev(elf, r)
            if e is None: continue
            if e.startswith("str:"): last = e[4:].replace("[fuelgauge_ic]%s:%d ", "")[:60]
            if e.startswith("call:"):
                nm = e[5:]
                tok = nm
                if nm.startswith("__mca_log") or nm == "mca_log_" : tok = nm + "(" + str(last) + ")"
                out.append((a, tok))
    return out
s = seq(S, "stf/bq_fg_probe.s"); o = seq(O, "ourf/bq_fg_probe.s")
sm = difflib.SequenceMatcher(None, [x[1] for x in s], [x[1] for x in o], autojunk=False)
print("stock calls", len(s), "ours", len(o))
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag == "equal": 
        continue
    print("%s stock[%d:%d]@%x-%x -> ours[%d:%d]" % (tag, i1, i2, s[i1][0] if i1 < len(s) else 0, s[i2-1][0] if i2 > i1 else 0, j1, j2))
    print("   S:", ", ".join(x[1] for x in s[i1:i2])[:900])
    print("   O:", ", ".join(x[1] for x in o[j1:j2])[:500])
