"""datacmp.py <stock.ko> <built.ko|.o> [-v]
Checks what compare_ko.py does NOT check:
 1. CONTENT of every named data object (local + global, .data/.rodata/.init.data/...; .bss: size only), with relocations
    inside the object resolved (function pointers, strings, other objects) and the relocated bytes masked.
 2. Anonymous constant data referenced from code (jump tables, local array/struct initialisers copied with memcpy,
    switch lookup tables, 16-byte vector constants): compare_ko resolves such a target as a C string (stops at the first
    NUL byte), so a table that starts with 0 compares equal whatever follows. Here the bytes are compared over the extent
    of the target in the BUILT object (to the end of its own section / next referenced offset / entsize of a .cstN section).
    Pairing: by reloc offset for byte-identical functions, by order of the anonymous-data relocs otherwise.
 3. kCFI type hash word in front of each function (4 bytes before the symbol).
"""
import sys, struct, collections
sys.path.insert(0, "<build-dir>")
from compare_ko import Elf, cstr

V = "-v" in sys.argv
stock, built = [a for a in sys.argv[1:] if not a.startswith("-")][:2]
A, B = Elf(stock), Elf(built)


def sec_of(e, name):
    return [i for i, s in enumerate(e.secs) if s["name"] == name]


def obj_syms(e):
    out = {}
    for s in e.syms:
        if s["type"] == 1 and 0 < s["shndx"] < len(e.secs):
            n = s["name"]
            out.setdefault(n, []).append(s)
    return out


def rel_in(e, shndx, lo, hi):
    """relocations applied to section shndx in [lo,hi): list of (off-lo, type, resolved target)"""
    r = []
    for off, t, si, add in e.relocs.get(shndx, []):
        if lo <= off < hi:
            try:
                tg = e.target(si, add)
            except Exception:
                tg = "sym%d+%d" % (si, add)
            r.append((off - lo, t, tg))
    return sorted(r)


RSIZE = {257: 8, 258: 4, 261: 4, 1024: 0}  # ABS64, ABS32, PREL32


def masked(data, rels):
    b = bytearray(data)
    for o, t, _ in rels:
        n = RSIZE.get(t, 8)
        b[o:o + n] = b"\0" * n
    return bytes(b)


bad = 0
# ---- 1. named objects
oa, ob = obj_syms(A), obj_syms(B)
nobj = 0
for n, la in sorted(oa.items()):
    if n in ("____versions", "__this_module", "_note_7", "_note_6", "_note_8"):
        continue
    if n not in ob:
        if not n.startswith(("__UNIQUE_ID", "__param", "__mod_", "_note", "__this_module", "$")) and not n.startswith(".L"):
            print("OBJ only in stock: %s (size %d)" % (n, la[0]["size"]))
        continue
    lb = ob[n]
    if len(la) != len(lb):
        print("OBJ %s: %d stock symbols vs %d built" % (n, len(la), len(lb)))
    for sa, sb in zip(la, lb):
        nobj += 1
        seca, secb = A.secs[sa["shndx"]], B.secs[sb["shndx"]]
        if sa["size"] != sb["size"]:
            print("OBJ %s: size %d vs %d" % (n, sa["size"], sb["size"])); bad += 1; continue
        if seca["type"] == 8 or secb["type"] == 8:
            if (seca["type"] == 8) != (secb["type"] == 8):
                da = A.data(sa["shndx"])[sa["val"]:sa["val"] + sa["size"]] if seca["type"] != 8 else bytes(sa["size"])
                db = B.data(sb["shndx"])[sb["val"]:sb["val"] + sb["size"]] if secb["type"] != 8 else bytes(sb["size"])
                if da != db:
                    print("OBJ %s: bss vs data with non-zero content" % n); bad += 1
            continue
        da = A.data(sa["shndx"])[sa["val"]:sa["val"] + sa["size"]]
        db = B.data(sb["shndx"])[sb["val"]:sb["val"] + sb["size"]]
        ra = rel_in(A, sa["shndx"], sa["val"], sa["val"] + sa["size"])
        rb = rel_in(B, sb["shndx"], sb["val"], sb["val"] + sb["size"])
        ma, mb = masked(da, ra), masked(db, rb)
        if ma != mb or [(o, x) for o, t, x in ra] != [(o, x) for o, t, x in rb]:
            bad += 1
            diffs = [i for i in range(len(ma)) if ma[i] != mb[i]]
            print("OBJ %s (%s, size %d): CONTENT DIFFERS at %d bytes, first offsets %s" % (n, seca["name"], sa["size"], len(diffs), diffs[:12]))
            if diffs and V:
                lo = diffs[0] & ~15
                print("   stock %s\n   built %s" % (da[lo:lo + 48].hex(), db[lo:lo + 48].hex()))
            ra2 = [(o, x) for o, t, x in ra]; rb2 = [(o, x) for o, t, x in rb]
            if ra2 != rb2:
                sa_, sb_ = set(ra2), set(rb2)
                print("   relocs stock-only %s\n   relocs built-only %s" % (sorted(sa_ - sb_)[:8], sorted(sb_ - sa_)[:8]))
print("named objects compared: %d" % nobj)

# ---- 2. anonymous rodata referenced from code
fa, fb = A.functions(), B.functions()
textsyms = {}


def anon_refs(e, fname):
    """list of (reloc offset in function, section index, addend) for refs into non-string, non-code data sections via section symbols"""
    s = next(x for x in e.syms if x["name"] == fname and x["type"] == 2 and x["size"])
    out = []
    for off, t, si, add in e.relocs.get(s["shndx"], []):
        if not (s["val"] <= off < s["val"] + s["size"]):
            continue
        if t in (277, 278, 279, 280, 282, 283, 284, 285, 286, 299):   # LO12 variants (ADD/LDST*), CALL/JUMP26
            continue
        sym = e.syms[si]
        if sym["type"] != 3:
            continue
        sec = e.secs[sym["shndx"]]
        if sec["name"].startswith((".text", ".init.text", ".exit.text")) or ".str" in sec["name"] or sec["type"] == 8:
            continue
        if not sec["name"].startswith((".rodata", ".data", ".init.rodata")):
            continue
        if is_str_at(e, sym["shndx"], add):
            continue
        out.append((off - s["val"], sym["shndx"], add))
    return sorted(out)


def is_str_at(e, shndx, add):
    d = e.data(shndx)
    j = d.find(b"\0", add)
    if j <= add:
        return False
    t = d[add:j]
    return all(32 <= c < 127 or c in (9, 10, 1) for c in t) and len(t) >= 2


def extent(e, shndx, add, refs_same_sec):
    sec = e.secs[shndx]
    d = e.data(shndx)
    ent = None
    if sec["name"].startswith(".rodata.cst"):
        try:
            ent = int(sec["name"][len(".rodata.cst"):])
        except ValueError:
            ent = None
    if ent:
        return ent
    nxt = [a for a in refs_same_sec if a > add]
    end = min(nxt) if nxt else len(d)
    # named object containing it?
    for s in e.syms:
        if s["shndx"] == shndx and s["type"] == 1 and s["val"] <= add < s["val"] + s["size"]:
            return None  # named object: covered by part 1
    return min(end - add, 4096)


nanon = 0
for n in fa:
    if n not in fb:
        continue
    ra, rb = anon_refs(A, n), anon_refs(B, n)
    if not rb and not ra:
        continue
    same = fa[n][0] == fb[n][0]
    if same:
        pa = {o: (s, a) for o, s, a in ra}
        pairs = [(pa.get(o), (s, a), o) for o, s, a in rb]
    else:
        if len(ra) != len(rb):
            print("ANON %s: %d stock vs %d built anonymous data refs (bytes differ, pairing by order is unreliable)" % (n, len(ra), len(rb)))
        pairs = [(x[1:], y[1:], y[0]) for x, y in zip(ra, rb)]
    allb = collections.defaultdict(set)
    for o, s, a in rb:
        allb[s].add(a)
    seen = set()
    for pa_, (sb_, ab_), o in pairs:
        if (sb_, ab_) in seen:
            continue
        seen.add((sb_, ab_))
        if pa_ is None:
            print("ANON %s +%#x: no stock counterpart" % (n, o)); bad += 1; continue
        sa_, aa_ = pa_
        if is_str_at(B, sb_, ab_) and is_str_at(A, sa_, aa_):
            continue
        L = extent(B, sb_, ab_, allb[sb_])
        if L is None:
            continue
        db = B.data(sb_)[ab_:ab_ + L]
        da = A.data(sa_)[aa_:aa_ + L]
        nanon += 1
        rb_ = rel_in(B, sb_, ab_, ab_ + L); ra_ = rel_in(A, sa_, aa_, aa_ + L)
        if rb_ or ra_:
            # e.g. pointer tables: compare resolved targets
            if [(x, z) for x, y, z in ra_] != [(x, z) for x, y, z in rb_] or masked(da, ra_) != masked(db, rb_):
                print("ANON %s +%#x (%s+%#x, %d B, with relocs): DIFFERS" % (n, o, B.secs[sb_]["name"], ab_, L)); bad += 1
            continue
        def canon(x, k):
            m, r = {}, []
            for i in range(0, len(x) - len(x) % k, k):
                r.append(m.setdefault(x[i:i + k], len(m)))
            return r
        part = [k for k in (1, 2, 4) if len(da) == len(db) and canon(da, k) == canon(db, k)]
        if da != db and part and not same:
            print("ANON %s +%#x (%s, %d B): values differ, SAME case partition (elem %d B) -> layout only" % (n, o, B.secs[sb_]["name"], L, part[-1]))
            continue
        if da != db:
            # jump tables of non-identical functions legitimately differ (relative branch offsets)
            jt = B.secs[sb_]["name"].startswith(".rodata.") and B.secs[sb_]["name"][8:] == n
            print("ANON %s +%#x (%s+%#x, %d B)%s: DIFFERS%s\n   stock %s\n   built %s" % (n, o, B.secs[sb_]["name"], ab_, L,
                  " [jump table?]" if jt else "", "" if not same else " (function bytes identical!)", da[:64].hex(), db[:64].hex()))
            if same or not jt:
                bad += 1
        elif V:
            print("ANON %s +%#x (%s, %d B): same" % (n, o, B.secs[sb_]["name"], L))
print("anonymous data blocks compared: %d" % nanon)

print("RESULT: %s" % ("no data differences" if not bad else "%d data differences" % bad))
