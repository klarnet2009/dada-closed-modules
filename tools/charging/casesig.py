#!/usr/bin/env python3
"""casesig.py STOCK.ko OURS.ko fn [fn..] : per jump-table-case event sets (calls, strings, immediates, struct offsets), stock vs ours.
Needs listings: ../stf/<fn>.s ../ourf/<fn>.s (llvm-objdump -dr --no-show-raw-insn split per function)."""
import sys, re, collections, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_ko_v2 import Elf

def parse(path):
    ins = []   # [addr, mnem, ops, [relocs]]
    for l in open(path):
        r = re.match(r"^\s+[0-9a-f]{16}:\s+(R_AARCH64_\w+)\s+(\S+)", l)
        if r:
            if ins: ins[-1][3].append((r.group(1), r.group(2)))
            continue
        m = re.match(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$", l)
        if m:
            ins.append([int(m.group(1), 16), m.group(2), m.group(3).strip(), []])
    return ins

def cstr(elf, sym, off):
    for nm in (sym,):
        for i in elf.secidx.get(nm, []):
            d = elf.sdata(i)
            if off < len(d):
                e = d.find(b"\0", off)
                return d[off:e].decode("latin-1")
    return None

def reloc_ev(elf, rel):
    typ, tgt = rel
    m = re.match(r"^(.*?)(?:\+0x([0-9a-f]+))?$", tgt)
    sym, off = m.group(1), int(m.group(2) or "0", 16)
    if typ in ("R_AARCH64_CALL26", "R_AARCH64_JUMP26"):
        if sym.startswith(".text"):
            if sym == ".text":
                for sh, lst in elf.fsyms.items():
                    for y in lst:
                        if elf.secs[sh].name == ".text" and y.val == off: return "call:" + y.name
                return "call:text+%x" % off
            return "call:" + sym[6:]
        return "call:" + sym
    if typ == "R_AARCH64_ADD_ABS_LO12_NC":
        if sym.startswith(".rodata"):
            s = cstr(elf, sym, off)
            if s is not None and len(s) >= 2 and all(32 <= ord(c) < 127 or ord(c) in (9, 10) for c in s):
                return "str:" + s
        return "obj:" + re.sub(r"^\.(rodata|data|bss)\.", "", sym) + ("+%x" % off if off else "")
    return None

def table_of(elf, ins):
    # find adrp+reloc to rodata then ldrh/ldrb with lsl, adr base
    tsym = None
    for i in ins:
        for typ, tgt in i[3]:
            if typ == "R_AARCH64_ADR_PREL_PG_HI21" and "rodata" in tgt and "str" not in tgt:
                m = re.match(r"^(.*?)(?:\+0x([0-9a-f]+))?$", tgt)
                tsym = (m.group(1), int(m.group(2) or "0", 16)); break
        if tsym: break
    return tsym

def cases(elf, ins):
    ts = table_of(elf, ins)
    # table section
    d = None
    for i in elf.secidx.get(ts[0], []):
        d = elf.sdata(i)
    esz = 2
    base = None
    for k, i in enumerate(ins):
        if i[1] == "adr":
            m = re.search(r"0x([0-9a-f]+)", i[2]); base = int(m.group(1), 16)
        if i[1].startswith("ldr") and "lsl" in i[2] and i[1] in ("ldrb", "ldrh", "ldrsw", "ldrsb", "ldrsh"):
            esz = {"ldrb": 1, "ldrsb": 1, "ldrh": 2, "ldrsh": 2, "ldrsw": 4}[i[1]]
    # trim table length: find via cmp bound is heuristic; use sym next boundary: whole remaining until a plausible end
    n = {1: 0, 2: 0, 4: 0}
    ent = []
    p = ts[1]
    addrs = set(i[0] for i in ins)
    nmax = int(os.environ.get("NENT", "100000"))
    while p + esz <= len(d) and len(ent) < nmax:
        v = int.from_bytes(d[p:p + esz], "little")
        a = base + v * 4
        if a not in addrs:
            break
        ent.append(a); p += esz
    return ent, esz, ts

def succ(ins, idx, byaddr):
    a, mn, ops, _ = ins[idx]
    tg = None
    m = re.search(r"0x([0-9a-f]+)", ops) if (mn == "b" or mn.startswith("b.") or mn in ("cbz", "cbnz", "tbz", "tbnz")) else None
    if m: tg = int(m.group(1), 16)
    nxt = ins[idx + 1][0] if idx + 1 < len(ins) else None
    if mn in ("ret", "br", "retaa"): return []
    if mn == "b": return [tg]
    if mn == "bl": return [nxt]
    if tg is not None: return [tg, nxt]
    return [nxt]

def analyze(elf, path, entry_off=0):
    ins = parse(path)
    byaddr = {i[0]: k for k, i in enumerate(ins)}
    ent, esz, ts = cases(elf, ins)
    reach = []
    for a in ent:
        seen = set(); st = [a]
        while st:
            x = st.pop()
            if x in seen or x not in byaddr: continue
            seen.add(x)
            for s in succ(ins, byaddr[x], byaddr):
                if s is not None: st.append(s)
        reach.append(seen)
    cnt = collections.Counter(x for r in reach for x in r)
    # tail = reached by >= 6 distinct case ENTRIES that are different blocks
    tail = set(x for x, c in cnt.items() if c >= 6)
    sigs = []
    for r in reach:
        ev = set()
        for x in r - tail:
            i = ins[byaddr[x]]
            for rel in i[3]:
                e = reloc_ev(elf, rel)
                if e: ev.add(e)
            mm = re.match(r"^(mov|movk|movz)\s+w\d+,\s*#(-?0x[0-9a-f]+|-?\d+)", i[1] + " " + i[2])
            if mm: ev.add("imm:" + str(int(mm.group(2), 0)))
            mm = re.match(r"^(cmp|cmn)\s+[wx]\d+,\s*#(0x[0-9a-f]+|\d+)", i[1] + " " + i[2])
            if mm: ev.add("cmp:" + str(int(mm.group(2), 0)))
            mm = re.match(r"^(ldr|ldrb|ldrh|ldrsw|str|strb|strh)\s+\w+,\s*\[(x\d+),\s*#(0x[0-9a-f]+)\]", i[1] + " " + i[2])
            if mm and mm.group(2) != "sp" and int(mm.group(3), 16) >= 0x100: ev.add("fld:%s%s" % (mm.group(1)[0:3], mm.group(3)))
        sigs.append(ev)
    return ent, esz, ts, sigs, len(tail)

if __name__ == "__main__":
    S, O = Elf(sys.argv[1]), Elf(sys.argv[2])
    for fn in sys.argv[3:]:
        es, ss, ts, sigs_s, tl_s = analyze(S, "stf/%s.s" % fn)
        eo, so, to, sigs_o, tl_o = analyze(O, "ourf/%s.s" % fn)
        print("== %s: stock %d entries (esz %d, tail %d) ours %d entries (esz %d, tail %d)" % (fn, len(es), ss, tl_s, len(eo), so, tl_o))
        off = int(os.environ.get("IDOFF", "0"))
        for k in range(max(len(es), len(eo))):
            a = sigs_s[k] if k < len(sigs_s) else None
            b = sigs_o[k] if k < len(sigs_o) else None
            same = (a == b)
            if os.environ.get("ALL") or not same:
                print("id %d: %s" % (k + off, "SAME" if same else "DIFF"))
                if not same:
                    print("   S-only:", sorted(a - b)[:12] if a is not None and b is not None else a)
                    print("   O-only:", sorted(b - a)[:12] if a is not None and b is not None else b)
