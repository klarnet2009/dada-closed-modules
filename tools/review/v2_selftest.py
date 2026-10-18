"""Mutation self-test of compare_ko_v2.py: damage ONE thing in a copy of a stock module and check that exactly the expected check turns to FAIL.
Usage (server): python3 v2_selftest.py <dir with compare_ko_v2.py> <work dir> <stock.ko> [<stock2.ko> ...]
A mutation that does not apply to a module (no jump table, ...) is skipped; every mutation must be exercised by at least one module."""
import os, struct, sys
sys.path.insert(0, sys.argv[1])
import compare_ko_v2 as c

work = sys.argv[2]
os.makedirs(work, exist_ok=True)
done, bad = {}, 0


def fails(stock, mut):
    _, _, res, _ = c.run(stock, mut)
    return {n for n, ok, *_ in res if not ok}


def mutate(stock, name, expect, fn):
    """fn(bytearray, Elf) -> True if it changed something"""
    global bad
    e = c.Elf(stock)
    d = bytearray(e.d)
    if not fn(d, e):
        return
    out = os.path.join(work, name + ".ko")
    open(out, "wb").write(d)
    f = fails(stock, out)
    ok = expect <= f
    done[name] = done.get(name, False) or ok
    print("%-4s %-28s expect %-34s got %s  (%s)" % ("OK" if ok else "MISS", name, sorted(expect), sorted(f), os.path.basename(stock)))
    bad += not ok


def sec_first(e, name):
    i = e.secidx.get(name)
    return i[0] if i else None


def m_func_byte(d, e):
    for sh in sorted(e.fsyms):
        for s in e.fsyms[sh]:
            if s.size >= 16 and e.kind[sh] == "code":
                d[e.secs[sh].off + s.val + s.size - 8] ^= 0x10
                return True


def m_reloc_addend(d, e):
    for sh, lst in e.rel.items():
        if e.kind[sh] == "code":
            for off, t, sy, add in lst:
                if t == 283:   # CALL26
                    # find rela entry file offset
                    for s in e.secs:
                        if s.type == 4 and s.info == sh:
                            for o in range(s.off, s.off + s.size, 24):
                                if struct.unpack_from("<Q", d, o)[0] == off:
                                    struct.pack_into("<q", d, o + 16, add + 4)
                                    return True


def m_obj_content(d, e):
    for sh in sorted(e.osyms):
        sec = e.secs[sh]
        if e.kind[sh] == "data" and not sec.flags & 1 and sec.type != 8:
            for s in e.osyms[sh]:
                if s.size >= 16 and not e.conststr(sh, s) and not s.name.startswith("$"):
                    d[sec.off + s.val + s.size - 1] ^= 0x01
                    return True


def m_anon(d, e):
    e.functions()
    e.objects()
    for (sh, addr), h in list(e.anon.items()):
        sec = e.secs[sh]
        if sec.type != 8 and h != "zero":
            hi = e.next_boundary(sh, addr)
            if hi - addr >= 8:       # flip the LAST non-zero byte of the chunk: a C-string comparison would miss it when a NUL comes first
                j = hi - 1
                while j > addr and d[sec.off + j] == 0:
                    j -= 1
                d[sec.off + j] ^= 0x04
                return True


def m_kcfi(d, e):
    for sh in sorted(e.fsyms):
        for s in e.fsyms[sh]:
            if e.preamble(sh, s.val) is not None:
                d[e.secs[sh].off + s.val - 4] ^= 0x01
                return True


def m_bug_flags(d, e):
    i = sec_first(e, "__bug_table")
    if i is not None and e.secs[i].size >= 12:
        d[e.secs[i].off + 10] ^= 0x01
        return True


def m_bug_line(d, e):
    i = sec_first(e, "__bug_table")
    if i is not None and e.secs[i].size >= 12:
        d[e.secs[i].off + 8] ^= 0x01
        return True


def m_jump(d, e):
    i = sec_first(e, "__jump_table")
    if i is not None:
        for s in e.secs:
            if s.type == 4 and s.info == i:
                for o in range(s.off, s.off + s.size, 24):
                    if struct.unpack_from("<Q", d, o)[0] % 16 == 8:      # key field: flip the "branch" low bit of the addend
                        a, = struct.unpack_from("<q", d, o + 16)
                        struct.pack_into("<q", d, o + 16, a ^ 1)
                        return True


def m_versions(d, e):
    i = sec_first(e, "__versions")
    if i is not None and e.secs[i].size >= 64:
        d[e.secs[i].off + 64 * 2] ^= 0x01
        return True


def m_kcrc(d, e):
    for n in ("__kcrctab", "__kcrctab_gpl"):
        i = sec_first(e, n)
        if i is not None and e.secs[i].size >= 4:
            d[e.secs[i].off] ^= 0x01
            return True


def modinfo_flip(prefix, delta):
    def f(d, e):
        i = sec_first(e, ".modinfo")
        data = e.sdata(i)
        k = data.find(prefix.encode())
        if k >= 0:
            d[e.secs[i].off + k + len(prefix) + delta] ^= 0x01
            return True
    return f


def m_note(d, e):
    i = sec_first(e, ".note.Linux")
    if i is not None:
        d[e.secs[i].off + e.secs[i].size - 1] ^= 0x01
        return True


def m_string(d, e):
    e.functions()
    for (si, add), r in list(e._tgt.items()):
        if r.startswith("str:") and len(r) > 8 and e.secs[e.syms[si].shndx].type != 8:
            s = e.syms[si]
            sh = s.shndx
            if sh and e.kind[sh] in ("str", "data"):
                d[e.secs[sh].off + s.val + add + 1] ^= 0x01
                return True


def m_param(d, e):
    i = sec_first(e, "__param")
    if i is not None and e.secs[i].size >= 40:
        d[e.secs[i].off + 24] ^= 0x01        # perm
        return True


def m_data_init(d, e):                         # initial value of a writable named object (the minet / vip_sched thresholds case)
    for sh in sorted(e.osyms):
        sec = e.secs[sh]
        if e.kind[sh] == "data" and sec.flags & 1 and sec.type != 8:
            for s in e.osyms[sh]:
                if s.size >= 4 and not s.name.startswith(("$", "__UNIQUE", "_note")) and sh not in e.rel:
                    d[sec.off + s.val] ^= 0x01
                    return True


TESTS = [("func-byte", {"function-bytes"}, m_func_byte), ("reloc-addend", {"function-relocs"}, m_reloc_addend),
         ("const-object", {"objects"}, m_obj_content), ("writable-object-init", {"objects"}, m_data_init),
         ("anon-last-byte", {"anon-data", "function-relocs"}, m_anon), ("kcfi-hash", {"kcfi-preamble"}, m_kcfi),
         ("bug-flags", {"bug-table"}, m_bug_flags), ("bug-line", {"bug-lines"}, m_bug_line), ("jump-key", {"jump-table"}, m_jump),
         ("import-crc", {"imports"}, m_versions), ("export-crc", {"exports"}, m_kcrc), ("modinfo-license", {"modinfo"}, modinfo_flip("license=", 0)),
         ("modinfo-depends", {"depends"}, modinfo_flip("depends=", 0)), ("vermagic-flags", {"vermagic"}, modinfo_flip("vermagic=", 56)),
         ("note", {"notes"}, m_note), ("string-ref", {"function-relocs"}, m_string), ("param-perm", {"other-tables"}, m_param)]
for stock in sys.argv[3:]:
    base = fails(stock, stock)
    print("baseline", os.path.basename(stock), sorted(base))
    for name, exp, fn in TESTS:
        mutate(stock, name, exp, fn)
print("not exercised by any module:", [n for n, *_ in TESTS if n not in done])
print("SELFTEST", "FAILED" if bad or any(not v for v in done.values()) else "OK")
