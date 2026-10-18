"""tblcmp.py <stock.ko> <built.ko|.o>
Compares the runtime-patching / metadata tables that compare_ko.py ignores, entry by entry with relocations resolved
(function+offset, string content, symbol name): __bug_table (BUG vs WARN flags, line), __jump_table (static keys),
.altinstructions, __ex_table, .static_call_sites, __patchable_function_entries, .init_array/.ctors, __param, __ksymtab*.
Entries are compared as multisets (link order differs). File-name strings are reduced to their basename."""
import sys, struct, collections, os
sys.path.insert(0, "<build-dir>")
from compare_ko import Elf, cstr

SECS = {"__bug_table": 12, "__jump_table": 16, ".altinstructions": 12, "__ex_table": 12, ".static_call_sites": 8,
        "__patchable_function_entries": 8, ".init_array": 8, "__param": 40, "__kcrctab": 4, "__kcrctab_gpl": 4}
RSIZE = {257: 8, 258: 4, 260: 8, 261: 4, 262: 2}  # ABS64 ABS32 PREL64 PREL32 PREL16


def tgt(e, si, add):
    s = e.syms[si]
    if s["type"] == 3:
        sec = e.secs[s["shndx"]]
        if sec["name"].startswith((".text", ".init.text", ".exit.text")):
            return "@" + e.owner(s["shndx"], add)
        if sec["type"] != 8 and (sec["name"].startswith(".rodata") or ".str" in sec["name"]):
            try:
                v = cstr(e.data(s["shndx"]), add)
                if "/" in v and v.endswith((".c", ".h")):
                    v = os.path.basename(v)
                return "str:" + v
            except Exception:
                pass
        return "@" + e.owner(s["shndx"], add)
    return "%s+%d" % (s["name"], add)


def entries(e, name, size):
    idx = [i for i, s in enumerate(e.secs) if s["name"] == name]
    out = collections.Counter()
    for i in idx:
        d = e.data(i) if e.secs[i]["type"] != 8 else b""
        rels = {}
        for off, t, si, add in e.relocs.get(i, []):
            rels.setdefault(off, []).append((t, si, add))
        for base in range(0, len(d) - len(d) % size, size):
            b = bytearray(d[base:base + size])
            r = []
            for o in range(base, base + size):
                for t, si, add in rels.get(o, []):
                    n = RSIZE.get(t, 4)
                    b[o - base:o - base + n] = b"\0" * n
                    # jump_table key carries flag bits in the addend low bits
                    r.append((o - base, t, tgt(e, si, add)))
            out[(bytes(b).hex(), tuple(r))] += 1
    return out


A, B = Elf(sys.argv[1]), Elf(sys.argv[2])
bad = 0
for name, size in SECS.items():
    ea, eb = entries(A, name, size), entries(B, name, size)
    if not ea and not eb:
        continue
    if ea == eb:
        print("OK   %-30s %d entries" % (name, sum(ea.values())))
        continue
    bad += 1
    print("DIFF %-30s stock %d / built %d entries" % (name, sum(ea.values()), sum(eb.values())))
    for k in list((ea - eb).elements())[:6]:
        print("   stock-only:", k)
    for k in list((eb - ea).elements())[:6]:
        print("   built-only:", k)
print("RESULT:", "tables identical" if not bad else "%d tables differ" % bad)
