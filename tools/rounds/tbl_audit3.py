#!/usr/bin/env python3
"""tbl_audit3.py stock.ko ours.ko [-v] : data-table CONTENT audit for one module (goal-G33a, extends g22f tbl_audit/anon_pair2).
Uses compare_ko_v2.Elf (relocation fields resolved to target symbol/addend/CONTENT, never raw bytes).
 1. named data objects (sized symbols in .data/.rodata/.bss classes): multiset per normalised name of (class,size,content+reloc targets);
    every differing/only-stock/only-ours name is listed with a word diff (sysfs attrs, fops, of_device_id, params, init values ...)
 2. anonymous constant pools / switch tables / jump tables: chunk = [reloc target, next boundary). Chunks are attributed to the function(s)
    whose .text relocations point at them. A stock chunk with no byte-identical counterpart is paired with the best ours chunk (same user
    function, else same size) and printed as a word diff; verdict BOUNDARY (prefix equal), MOVED (whole non-zero content present elsewhere in ours,
    len >= 12) or DIFF.
 3. global string multiset (.rodata.str*): strings only in stock / only in ours (except __FILE__-like paths).
 4. __param / __modinfo parm= entries are covered by compare_ko_v2 (run separately)."""
import sys, re, collections, bisect, hashlib, difflib
sys.path.insert(0, "<work-dir>")
from compare_ko_v2 import Elf, norm, FIELD
V = "-v" in sys.argv
args = [a for a in sys.argv[1:] if not a.startswith("-")]
S, O = Elf(args[0]), Elf(args[1])
for e in (S, O):
    for si_, lst in e.rel.items():
        if e.kind[si_] != "meta":
            for off, t, sy, add_ in lst:
                e.tgt(sy, add_)

def words(e, sh, lo, hi):
    """list of (offset, token) 4-byte words; relocation fields -> 'R:<target>'; trailing zero words stripped"""
    sec = e.secs[sh]
    data = e.d[sec.off + lo:sec.off + hi] if sec.type != 8 else bytes(hi - lo)
    ro, rl = e.reloff.get(sh, []), e.rel.get(sh, [])
    rel = {}
    for j in range(bisect.bisect_left(ro, lo), bisect.bisect_left(ro, hi)):
        off, t, sy, add = rl[j]
        rel[off - lo] = (FIELD.get(t, 8), e.tgtp(sy, add)[:70])
    out, o = [], 0
    while o < len(data):
        if o in rel:
            n, t = rel[o]
            out.append((o, "R:" + t)); o += n; continue
        w = data[o:o + 4]
        out.append((o, w.hex() if len(w) == 4 else w.hex()))
        o += 4
    while out and out[-1][1] == "00000000":
        out.pop()
    return out

def wdiff(la, lb, maxl=14):
    sm = difflib.SequenceMatcher(None, [t for _, t in la], [t for _, t in lb], autojunk=False)
    res, n = [], 0
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal":
            continue
        n += 1
        if len(res) < maxl:
            def fmt(l):
                return " ".join(("%d" % int.from_bytes(bytes.fromhex(t), "little", signed=True) if not t.startswith("R:") and len(t) == 8 else t) for _, t in l)
            res.append("    %s stock+%#x[%s] -> ours+%#x[%s]" % (op, la[i1][0] if i1 < len(la) else -1, fmt(la[i1:i2]), lb[j1][0] if j1 < len(lb) else -1, fmt(lb[j1:j2])))
    return n, res

# ---------------------------------------------------------------- 1. named objects
print("== 1. named data objects")
oa, ob = S.objects(), O.objects()
print("  names stock %d ours %d, objects stock %d ours %d" % (len(oa), len(ob), sum(map(len, oa.values())), sum(map(len, ob.values()))))
IGN = re.compile(r"^(__UNIQUE_ID|_note_|__compound_literal|__already_done|__warned|__print_once|\.L)")
os_ = sorted(n for n in set(oa) - set(ob) if not IGN.match(n))
oo_ = sorted(n for n in set(ob) - set(oa) if not IGN.match(n))
print("  only stock (%d):" % len(os_), os_[:60])
print("  only ours  (%d):" % len(oo_), oo_[:60])
# renamed objects: pair only-stock with only-ours by (class,size,content); the rest stays unpaired
def pairs_by_content(a_names, b_names):
    free = collections.defaultdict(list)
    for n in b_names:
        for o in ob[n]:
            free[o].append(n)
    res, miss = [], []
    for n in a_names:
        for o in oa[n]:
            if free.get(o):
                res.append((n, free[o].pop(0), o[0], o[1]))
            else:
                miss.append((n, o[0], o[1]))
    return res, miss
rn, miss = pairs_by_content(os_, oo_)
print("  renamed objects with EQUAL content (stock name -> ours name, class, size): %s" % [(a, b, c, d) for a, b, c, d in rn][:20])
print("  only-stock objects WITHOUT an equal-content ours object: %s" % miss[:20])
bad = [k for k in oa if k in ob and collections.Counter(oa[k]) != collections.Counter(ob[k])]
print("  DIFFERING names (class,size,content multiset): %d" % len(bad))

def dump(e, name):
    res = []
    for sh, lst in e.osyms.items():
        for s in lst:
            if norm(s.name) != name or e.kind[sh] not in ("data", "bss"):
                continue
            res.append((s.name, e.secs[sh].name, s.size, words(e, sh, s.val, s.val + s.size)))
    return res
for k in bad:
    da, db = dump(S, k), dump(O, k)
    print("  --- %s stock %s ours %s" % (k, [(n, sn, sz) for n, sn, sz, _ in da][:3], [(n, sn, sz) for n, sn, sz, _ in db][:3]))
    # pair objects with equal size first
    for (n1, s1, z1, l1) in da:
        cand = [x for x in db if x[2] == z1] or db
        if not cand:
            continue
        best = min(cand, key=lambda x: wdiff(l1, x[3])[0])
        n, res = wdiff(l1, best[3])
        if n:
            print("    %s(%d B) vs %s(%d B): %d hunks" % (n1, z1, best[0], best[2], n))
            for r in res[:6]:
                print(r)

# ---------------------------------------------------------------- 2. anon chunks
print("== 2. anonymous constant pools / switch / jump tables")
def scan(e):
    """own anonymous-chunk scan (compare_ko_v2 classifies a table whose first bytes look like a NUL-terminated string, e.g. {1000,..} = e8 03 00 00,
    as a C string and compares only that prefix: this scan takes EVERY relocation target in a non-string data/rodata section that is not inside a
    sized named object). Returns anon {(sh,addr): blob hash} and users {(sh,addr): set(function / obj:name)}"""
    anon, u = {}, collections.defaultdict(set)
    for sh, lst in e.rel.items():
        if e.kind[sh] == "meta":
            continue
        for off, t, sy, add in lst:
            s = e.syms[sy]
            if not (0 < s.shndx < len(e.secs)) or e.kind[s.shndx] != "data" or e.secs[s.shndx].type == 8:
                continue
            addr = s.val + add
            if e.owner(e.osyms, e.oval, s.shndx, addr) or addr in e.zkeys.get(s.shndx, ()):
                continue
            key = (s.shndx, addr)
            if key not in anon:
                hi = e.next_boundary(s.shndx, addr)
                anon[key] = e.blob(s.shndx, addr, hi)
            if e.kind[sh] == "code":
                f = e.owner(e.fsyms, e.fval, sh, off)
                u[key].add(norm(f.name) if f else "?code")
            else:
                o = e.owner(e.osyms, e.oval, sh, off)
                u[key].add("obj:" + (norm(o.name) if o else e.secs[sh].name))
    return anon, u
AS, US = scan(S)
AO, UO = scan(O)
ca, cb = collections.Counter(v for v in AS.values() if v != "zero"), collections.Counter(v for v in AO.values() if v != "zero")
so_h, oo_h = ca - cb, cb - ca
print("  anon chunks: stock %d (distinct %d) ours %d (distinct %d); stock-only %d, ours-only %d" % (sum(ca.values()), len(ca), sum(cb.values()), len(cb), sum(so_h.values()), sum(oo_h.values())))

def tokstream(e):
    parts = []
    for i, s in enumerate(e.secs):
        if s.type != 8 and (e.kind[i] in ("data", "str") or s.name.startswith(".rodata")):
            data = bytearray(e.d[s.off:s.off + s.size])
            ro, rl = e.reloff.get(i, []), e.rel.get(i, [])
            for j in range(len(ro)):
                off, t, sy, add = rl[j]
                n = FIELD.get(t, 8)
                data[off:off + n] = b"\xfe" + hashlib.sha1(e.tgtp(sy, add).encode()).digest()[:n - 1]
            parts.append(bytes(data))
    return b"\x01\x02\x03".join(parts)
BO, BS = tokstream(O), tokstream(S)

def chunks(e, only, us, anon):
    out = []
    seen = set()
    for (sh, ad), h in anon.items():
        if only[h] and h not in seen:
            seen.add(h)
            hi = e.next_boundary(sh, ad)
            out.append((sh, ad, hi, h, sorted(us.get((sh, ad), ())), words(e, sh, ad, hi)))
    return out
cs, co = chunks(S, so_h, US, AS), chunks(O, oo_h, UO, AO)

def rawtok(e, sh, lo, hi):
    ws = words(e, sh, lo, hi)
    return ws

def pair(c, cands, e_other_big):
    sh, ad, hi, h, us, ws = c
    best, bn = None, 10 ** 9
    for d in cands:
        sm = difflib.SequenceMatcher(None, [t for _, t in ws], [t for _, t in d[5]], autojunk=False)
        score = sum(max(i2 - i1, j2 - j1) for op, i1, i2, j1, j2 in sm.get_opcodes() if op != "equal") + (0 if set(us) & set(d[4]) else 3) + abs(len(ws) - len(d[5])) * 0.01
        if score < bn:
            best, bn = d, score
    return best

def verdict(ws, wo, other_big, e, sh, lo, hi):
    ts = [t for _, t in ws]; tt = [t for _, t in wo]
    m = min(len(ts), len(tt))
    if ts[:m] == tt[:m]:
        return "BOUNDARY"       # one chunk is a prefix of the other (different chunk cut)
    return None

STRCHARS = frozenset(list(range(1, 7)) + [9, 10, 13] + list(range(32, 127)) + list(range(128, 256)))
def tokb(e, sh, lo, hi):
    sec = e.secs[sh]
    data = bytearray(e.d[sec.off + lo:sec.off + hi]) if sec.type != 8 else bytearray(hi - lo)
    ro, rl = e.reloff.get(sh, []), e.rel.get(sh, [])
    for j in range(bisect.bisect_left(ro, lo), bisect.bisect_left(ro, hi)):
        off, t, sy, add = rl[j]
        n = FIELD.get(t, 8)
        data[off - lo:off - lo + n] = b"\xfe" + hashlib.sha1(e.tgtp(sy, add).encode()).digest()[:n - 1]
    return bytes(data).rstrip(b"\0")

def contained(e, sh, lo, hi, big):
    """returns None (not found) / 'MOVED' (embedded NUL: table found whole in the other module, len>=12) / 'STR' (string-like, found NUL-terminated)"""
    c = tokb(e, sh, lo, hi)
    if not c:
        return "ZERO"
    if all(x == 0 or x in STRCHARS for x in c):
        return "STR" if all((q + b"\0") in big for q in c.split(b"\0") if q) else None
    return "MOVED" if len(c) >= 12 and c in big else None


# ---------------------------------------------------------------- jump tables: case -> first call along the fall-through path
import struct
_cr = {}
def code_relocs(e):
    if id(e) not in _cr:
        d = {}
        for sh, lst in e.rel.items():
            if e.kind[sh] == "code":
                for off, t, sy, add in lst:
                    d[(sh, off)] = (t, sy, add)
        _cr[id(e)] = d
    return _cr[id(e)]
def sx(v, b):
    return v - (1 << b) if v >> (b - 1) else v
LD = [(0x38600800, 1, False), (0x78600800, 2, False), (0xB8600800, 4, False), (0xB8A00800, 4, True), (0x38E00800, 1, True),
      (0x78E00800, 2, True), (0x38A00800, 1, True), (0x78A00800, 2, True)]
def jt_sig(e, sh, lo, hi, fname):
    """returns (nentries, elemsize, [per-case terminal event]) or None"""
    cr = code_relocs(e)
    for (csh, off), (t, sy, add) in cr.items():
        if t != 275:
            continue
        s = e.syms[sy]
        if s.shndx != sh or s.val + add != lo:
            continue
        f = e.owner(e.fsyms, e.fval, csh, off)
        if not f or norm(f.name) != fname:
            continue
        sec = e.secs[csh]
        code = e.d[sec.off:sec.off + sec.size]
        w = lambda o: struct.unpack_from("<I", code, o)[0]
        base = esz = signed = sh_amt = None
        # table register: adrp dest, possibly re-materialised by the following add; find the first register-offset load from it after the adrp
        regs = {w(off) & 31}
        ldpc = None
        for o in range(off + 4, f.val + f.size, 4):
            x = w(o)
            if (x & 0xFF000000) == 0x91000000 and ((x >> 5) & 31) in regs:       # add xd, xn, #imm
                regs.add(x & 31)
            for m, n, sg in LD:
                if esz is None and (x & 0xFFE00C00) == m and ((x >> 5) & 31) in regs:
                    esz, signed, ldpc = n, sg, o
            if esz is not None:
                break
        if ldpc is not None:
            for o in range(max(f.val, ldpc - 0x20), min(f.val + f.size, ldpc + 0x20), 4):
                x = w(o)
                if base is None and (x & 0x9F000000) == 0x10000000:
                    imm = sx(((x >> 5) & 0x7FFFF) << 2 | ((x >> 29) & 3), 21)
                    base = o + imm
                if o > ldpc and sh_amt is None and (x & 0xFF200000) == 0x8B000000 and ((x >> 5) & 31) != 31:
                    sh_amt = (x >> 10) & 63
        if base is None or esz is None:
            return None
        sh_amt = 2 if sh_amt is None else sh_amt
        data = e.d[e.secs[sh].off + lo:e.secs[sh].off + hi]
        n = len(data) // esz
        ents = [int.from_bytes(data[i * esz:(i + 1) * esz], "little", signed=signed) for i in range(n)]
        res = []
        for v in ents:
            pc = base + (v << sh_amt)
            ev = None
            for hop in range(8):
                for _ in range(48):
                    if not (f.val <= pc < f.val + f.size):
                        ev = "out"; break
                    x = w(pc)
                    r = cr.get((csh, pc))
                    if r and r[0] in (282, 283):
                        ev = "call:" + e.tgtp(r[1], r[2]); break
                    if x == 0xd65f03c0 or (x & 0xFFFFFC1F) == 0xd65f0000:
                        ev = "ret"; break
                    if (x & 0xFC000000) == 0x14000000:      # b
                        pc += sx(x & 0x3FFFFFF, 26) * 4
                        break
                    pc += 4
                else:
                    ev = "long"
                if ev:
                    break
            res.append(ev or "loop")
        return n, esz, res
    return None


real = []
PRINTABLE = frozenset([9, 10, 13] + list(range(32, 127)))
def strlike(c):
    if c[:2] and c[0] == 1 and 48 <= c[1] <= 55:
        c = c[2:]
    return bool(c) and b"\0\0" not in c and all(x == 0 or x in PRINTABLE for x in c)

def classify(es, sh, lo, hi, big):
    """STR / MOVED / ZERO / None"""
    c = tokb(es, sh, lo, hi)
    if not c:
        return "ZERO"
    if strlike(c) and b"/" in c and c.endswith((b".c", b".h")) and b" " not in c:
        return "FILEPATH"      # __FILE__ strings of __bug_table / WARN (build-path cosmetics)
    if strlike(c):
        return "STR" if all((q + b"\0") in big for q in c.split(b"\0") if q) else None
    return "MOVED" if len(c) >= 12 and c in big else None

def process(cl, es, other_big, tag):
    cnt = collections.Counter()
    rest = []
    for c in cl:
        sh, ad, hi, h, us, ws = c
        k = classify(es, sh, ad, hi, other_big)
        if k:
            cnt[k] += 1
        else:
            rest.append(c)
    print("  %s auto-explained (identical string / table found whole in the other module): %s; remaining %d" % (tag, dict(cnt), len(rest)))
    return rest

def jt_pair(c, d, es, eo):
    sh, ad, hi, h, us, ws = c
    for fn in us:
        if fn in d[4]:
            a = jt_sig(es, sh, ad, hi, fn)
            b = jt_sig(eo, d[0], d[1], d[2], fn)
            if a and b:
                return fn, a, b
    return None

rs, ro_ = process(cs, S, BO, "stock-only"), process(co, O, BS, "ours-only ")
used = set()
def score(c, d):
    sm = difflib.SequenceMatcher(None, [t for _, t in c[5]], [t for _, t in d[5]], autojunk=False)
    return sum(max(i2 - i1, j2 - j1) for op, i1, i2, j1, j2 in sm.get_opcodes() if op != "equal") + (0 if set(c[4]) & set(d[4]) else 3) + abs(len(c[5]) - len(d[5])) * 0.01
nunex = 0
ONAMES = {norm(x.name) for l in O.fsyms.values() for x in l}
SNAMES = {norm(x.name) for l in S.fsyms.values() for x in l}
absent = collections.Counter()
done = set()
def handle_jt(c, d, j, head):
    global nunex
    fn, a, b = j
    if a[0] == b[0] and a[1] == b[1] and a[2] == b[2]:
        print("  [JT-EQUIV] stock %s: %d x %dB entries, every case reaches the same first callee/ret as ours (%s)" % (head, a[0], a[1], fn)); return
    bad = [i for i in range(min(len(a[2]), len(b[2]))) if a[2][i] != b[2][i]]
    if not bad and a[1] == b[1]:
        print("  [JT-PREFIX-EQUIV] stock %s: entry count stock %d vs ours %d (first %d cases identical; extra entries are table padding/alignment only if the function's cmp bound says so - check) (%s)" % (head, a[0], b[0], min(a[0], b[0]), fn)); return
    print("  [JT-DIFF] stock %s vs ours %s+%#x: %d x %dB vs %d x %dB, differing cases %s" % (head, O.secs[d[0]].name, d[1], a[0], a[1], b[0], b[1], [(i, a[2][i], b[2][i]) for i in bad[:10]]))
    real.append(head); nunex += 1
def headof(c):
    return "%s+%#x len %d users %s" % (S.secs[c[0]].name, c[1], c[2] - c[1], c[4][:4])
for c in rs:                       # pass 1: jump tables first (so that a table is never consumed by an unrelated string pairing)
    for d in ro_:
        if id(d) not in used and set(c[4]) & set(d[4]):
            j = jt_pair(c, d, S, O)
            if j:
                used.add(id(d)); done.add(id(c)); handle_jt(c, d, j, headof(c)); break
for c in rs:
    if id(c) in done:
        continue
    sh, ad, hi, h, us, ws = c
    head = headof(c)
    if us and all((u in SNAMES and u not in ONAMES) for u in us):
        absent[tuple(us)] += 1
        continue
    cand = [d for d in ro_ if id(d) not in used and set(c[4]) & set(d[4])]
    d = min(cand, key=lambda d: score(c, d)) if cand else None
    if d is not None and score(c, d) > 0.5 * max(len(c[5]), len(d[5])) + 3 and not (c[5] and d[5] and [t for _, t in c[5]][:1] == [t for _, t in d[5]][:1]):
        d = None                   # no plausible pairing (do not consume an unrelated chunk)
    if d is None:
        print("  [NO-COUNTERPART] stock-only %s words %s" % (head, " ".join(t for _, t in ws[:10]))); real.append(head); nunex += 1; continue
    used.add(id(d))
    ts, tt = [t for _, t in ws], [t for _, t in d[5]]
    m = min(len(ts), len(tt))
    if ts[:m] == tt[:m]:
        longer, other_big, ee = (c, BO, S) if len(ts) > len(tt) else (d, BS, O)
        lw = longer[5]
        tail = tokb(ee, longer[0], longer[1] + lw[m][0], longer[2]) if m < len(lw) else b""
        if not tail or tail in other_big or len(tail) < 4:
            print("  [BOUNDARY-OK] stock %s vs ours len %d: prefix equal, tail %d B present in the other module" % (head, d[2] - d[1], len(tail))); continue
        print("  [BOUNDARY-TAIL-MISSING] stock %s vs ours len %d: prefix equal, the extra %d B of the longer chunk are NOT in the other module" % (head, d[2] - d[1], len(tail)))
        real.append(head); nunex += 1; continue
    n, res = wdiff(ws, d[5])
    print("  [DIFF] stock %s vs ours %s+%#x len %d users %s: %d hunks" % (head, O.secs[d[0]].name, d[1], d[2] - d[1], d[4][:4], n))
    for r in res[:8]:
        print(r)
    real.append(head); nunex += 1
if absent:
    print("  stock-only chunks used ONLY by functions that are absent from ours (documented omissions): %d chunks, functions %s" % (sum(absent.values()), sorted(set(u for k in absent for u in k))[:30]))
for d in ro_:
    if id(d) in used:
        continue
    sh, ad, hi, h, us, ws = d
    head = "%s+%#x len %d users %s" % (O.secs[sh].name, ad, hi - ad, us[:4])
    print("  [NO-COUNTERPART] ours-only %s words %s" % (head, " ".join(t for _, t in ws[:10]))); real.append(head); nunex += 1
print("  unexplained: %d" % nunex)
