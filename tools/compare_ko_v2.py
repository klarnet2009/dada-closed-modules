#!/usr/bin/env python3
"""compare_ko_v2.py - STRICT equivalence checker for a stock kernel module vs a rebuilt one (stdlib only, run: python3 -I compare_ko_v2.py stock.ko built.ko [-v]).

Replaces the weak work/re/src compare_ko.py (holes found by the independent charging review). Checks, each reported PASS/FAIL with counts:
  function set / function bytes / function relocations (ALL functions, also those whose bytes differ; targets resolved to function+offset,
  object name or CONTENT, strings as byte ranges, ABS symbols incl. their value = kCFI typeids) / kCFI preamble word (the hash 4 bytes before the symbol)
  objects     every named OBJECT symbol (global AND static, .data/.rodata/.bss/.init.*): name, size, section class, CONTENT modulo relocations,
              relocation targets resolved
  anon-data   anonymous data (jump tables, memcpy sources, lookup tables, vector constants) = referenced chunk [target, next boundary),
              compared by content (NOT as a C string); strings referenced from anywhere are compared by content in the relocation descriptors
  bug-table / jump-table / other-tables (.altinstructions .ex_table .static_call_sites __param tracepoint/ftrace tables ...)  as multisets of entries
  exports (+CRC, kind, namespace) / imports (+CRC) / modinfo (all tags except depends+vermagic) / depends / vermagic (flag part) / notes
  INFO        imports that resolve to FORGED providers (depends= names that the stock does not have): the matching CRC does not prove the prototype.
Verdict STRICT-EQUIVALENT only if every check passes.  Options: -v (all details), --exports-map F (name<TAB>crc<TAB>module of the stock set, see --build-map),
--build-map DIR OUT (write that map from a directory of stock .ko), --batch LIST (lines: module<TAB>stock<TAB>built) [--csv OUT].
Known limits (documented, not hidden): relocation-less references cannot be seen; a table that starts with printable bytes followed by NUL inside a
non-string section is classified as a string; trailing zero bytes of anonymous chunks are ignored; inline asm bytes beyond code/relocs are not interpreted."""
import bisect, collections, hashlib, os, re, struct, sys

SHF_WRITE, SHF_ALLOC, SHF_EXEC = 1, 2, 4
FIELD = {257: 8, 258: 4, 259: 2, 260: 8, 261: 4, 262: 2}          # data relocation field sizes (ABS64/32/16, PREL64/32/16)
TABLES = {"__bug_table": 12, "__jump_table": 16, ".altinstructions": 12, "__ex_table": 12, ".static_call_sites": 8,
          "__patchable_function_entries": 8, ".init_array": 8, "__param": 40, "_ftrace_events": 8, "_ftrace_eval_map": 8,
          "__tracepoints_ptrs": 4, "__trace_printk_fmt": 8, "__bpf_raw_tp_map": 32, ".ctors": 8}
SPECIAL = {".modinfo", "__versions", ".altinstr_replacement", ".eh_frame", ".init.eh_frame", ".BTF", ".BTF_ids", ".comment"}
DATA_PFX = (".data", ".rodata", ".bss", ".init.data", ".init.rodata", ".exit.data", ".ref.data", ".gnu.linkonce", ".init.bss")
STRCH = frozenset(list(range(1, 7)) + [9, 10, 13] + list(range(32, 127)) + list(range(128, 256)))
ALT = "\x1e"     # separates alternative readings of an ambiguous relocation target (only used in function relocations)
Sec = collections.namedtuple("Sec", "name type flags off size link info entsize")
Sym = collections.namedtuple("Sym", "name type bind shndx val size")
sys.setrecursionlimit(20000)


def norm(n):
    """strip build-order noise from symbol names: .llvm.<hash>, trailing .N, __UNIQUE_ID counters, _note_N"""
    n = re.sub(r"\.llvm\.\d+$", "", n)
    if n.startswith("__UNIQUE_ID_"):
        return re.sub(r"\d+$", "", n)
    if n.startswith("_note_"):
        return "_note_"
    return re.sub(r"\.\d+$", "", n)


def cls(secname, secflags, sectype):
    """section class of an object: .data/.rodata/.bss with the init/exit/ro_after_init/read_mostly variants, per-object suffix dropped"""
    m = re.match(r"^(\.init|\.exit|\.ref)?(\.data|\.rodata|\.bss)(\.\.\w+)?", secname)
    base = (m.group(0) if m else secname)
    base = re.sub(r"\.\.(?!ro_after_init|read_mostly)\w*$", "", base)      # compiler-generated suffixes (..once, ..str, ..compoundliteral)
    if sectype == 8:
        base = base.replace(".data", ".bss")
    return base


class Elf:
    def __init__(self, path):
        d = self.d = open(path, "rb").read()
        if d[:4] != b"\x7fELF":
            raise ValueError("not ELF: " + path)
        shoff, = struct.unpack_from("<Q", d, 0x28)
        es, n, si = struct.unpack_from("<HHH", d, 0x3A)
        raw = [struct.unpack_from("<IIQQQQIIQQ", d, shoff + i * es) for i in range(n)]
        so = raw[si][4]
        self.secs = [Sec(d[so + r[0]:d.index(b"\0", so + r[0])].decode(), r[1], r[2], r[4], r[5], r[6], r[7], r[9]) for r in raw]
        self.secidx = collections.defaultdict(list)
        for i, s in enumerate(self.secs):
            self.secidx[s.name].append(i)
        sy = next(i for i, s in enumerate(self.secs) if s.type == 2)
        s = self.secs[sy]
        st = self.secs[s.link].off
        self.syms = [Sym(d[st + nm:d.index(b"\0", st + nm)].decode("latin-1"), info & 15, info >> 4, sh, val, size)
                     for nm, info, _o, sh, val, size in struct.iter_unpack("<IBBHQQ", d[s.off:s.off + s.size])]
        self.rel = {}
        for s in self.secs:
            if s.type == 4:
                lst = self.rel.setdefault(s.info, [])
                for off, info, add in struct.iter_unpack("<QQq", d[s.off:s.off + s.size]):
                    lst.append((off, info & 0xFFFFFFFF, info >> 32, add))
        for l in self.rel.values():
            l.sort()
        self.reloff = {k: [r[0] for r in v] for k, v in self.rel.items()}
        self.kind = [self._kind(i) for i in range(n)]
        # per-section sorted symbol arrays
        self.fsyms, self.osyms, self.maps, self.zkeys = {}, {}, {}, {}
        for s in sorted(self.syms, key=lambda x: (x.val, -x.size, x.name)):
            if not 0 < s.shndx < n:
                continue
            if s.type == 2 and s.size:
                self.fsyms.setdefault(s.shndx, []).append(s)
            elif s.type == 1 and s.size:
                self.osyms.setdefault(s.shndx, []).append(s)
            elif s.type == 1 and not s.size:      # zero-size objects = lockdep keys (struct lock_class_key is empty without lockdep)
                self.zkeys.setdefault(s.shndx, set()).add(s.val)
            elif s.name[:2] in ("$x", "$d") and s.type == 0:
                self.maps.setdefault(s.shndx, []).append((s.val, s.name[1]))
        self.fval = {k: [x.val for x in v] for k, v in self.fsyms.items()}
        self.oval = {k: [x.val for x in v] for k, v in self.osyms.items()}
        for v in self.maps.values():
            v.sort()
        self.mval = {k: [x[0] for x in v] for k, v in self.maps.items()}
        self._tgt, self._blob, self.anon, self.aninfo = {}, {}, {}, {}
        self._refs = None
        self.nstr = 0

    # ---- classification
    def _kind(self, i):
        s = self.secs[i]
        n = s.name
        if i == 0 or not s.flags & SHF_ALLOC or s.type in (2, 3, 4):
            return "meta"
        if n in SPECIAL or n.startswith((".note", ".hyp.", "__ksymtab", "__kcrctab")) and n != "__ksymtab_strings":
            return "special"
        if s.flags & SHF_EXEC:
            return "code"
        if re.match(r"^\.rodata\.str\d+\.\d+$", n) or n.endswith("_strings"):
            return "str"
        if n in TABLES:
            return "table"
        if n.startswith(DATA_PFX):
            return "bss" if s.type == 8 else "data"
        return "table"    # unknown alloc section (e.g. __dyndbg, .pci_fixup*): compared as entries of its entsize / 8

    def sdata(self, i):
        s = self.secs[i]
        return self.d[s.off:s.off + s.size] if s.type != 8 else bytes(s.size)

    def owner(self, table, vals, shndx, addr):
        v = vals.get(shndx)
        if not v:
            return None
        i = bisect.bisect_right(v, addr) - 1
        for j in range(i, max(i - 4, -1), -1):
            s = table[shndx][j]
            if s.val <= addr < s.val + s.size:
                return s
        return None

    def ender(self, shndx, addr):
        """sized object of the section whose end address is exactly addr (None if there is none)"""
        v = self.oval.get(shndx)
        if not v:
            return None
        i = bisect.bisect_left(v, addr) - 1
        for j in range(i, max(i - 4, -1), -1):
            s = self.osyms[shndx][j]
            if s.size and s.val + s.size == addr:
                return s
        return None

    # ---- boundaries of anonymous chunks
    def refs(self):
        if self._refs is None:
            r = collections.defaultdict(set)
            for si_, lst in self.rel.items():
                if self.kind[si_] == "meta":
                    continue
                for off, t, sy, add in lst:
                    s = self.syms[sy]
                    if 0 < s.shndx < len(self.secs) and self.kind[s.shndx] in ("data", "bss"):
                        r[s.shndx].add(s.val + add)
            self._refs = {k: sorted(v) for k, v in r.items()}
        return self._refs

    def next_boundary(self, shndx, lo):
        sec = self.secs[shndx]
        cands = [sec.size]
        rf = self.refs().get(shndx, [])
        i = bisect.bisect_right(rf, lo)
        if i < len(rf):
            cands.append(rf[i])
        ov = self.oval.get(shndx, [])
        i = bisect.bisect_right(ov, lo)
        if i < len(ov):
            cands.append(ov[i])
        m = re.match(r"^\.rodata\.cst(\d+)$", sec.name)
        if m:
            cands.append(lo + int(m.group(1)))
        return min(cands)

    # ---- content hash of a chunk (bytes modulo relocation fields + resolved relocation targets), trailing zeros ignored
    def blob(self, shndx, lo, hi):
        key = (shndx, lo, hi)
        if key in self._blob:
            return self._blob[key]
        self._blob[key] = "cycle"
        sec = self.secs[shndx]
        if sec.type == 8:
            r = "zero"
        else:
            data = bytearray(self.d[sec.off + lo:sec.off + hi])
            parts = []
            ro = self.reloff.get(shndx, [])
            rl = self.rel.get(shndx, [])
            for j in range(bisect.bisect_left(ro, lo), bisect.bisect_left(ro, hi)):
                off, t, sy, add = rl[j]
                n = FIELD.get(t, 8)
                data[off - lo:off - lo + n] = bytes(len(data[off - lo:off - lo + n]))
                parts.append("%d:%d:%s" % (off - lo, t, self.tgtp(sy, add)))
            c = bytes(data).rstrip(b"\0")
            r = "zero" if not c and not parts else hashlib.sha1(c + b"|" + "|".join(parts).encode()).hexdigest()[:20] + ":%d" % len(c)
        self._blob[key] = r
        return r

    # ---- relocation target -> canonical description
    def tgt(self, sy, add):
        k = (sy, add)
        r = self._tgt.get(k)
        if r is None:
            r = self._tgt[k] = self._tgt1(sy, add)
        return r

    def tgtp(self, sy, add):
        return self.tgt(sy, add).split(ALT)[0]

    def _tgt1(self, sy, add):
        s = self.syms[sy]
        if s.shndx == 0:
            return "ext:%s%s" % (s.name, "%+d" % add if add else "")
        if s.shndx == 0xfff1:
            return "abs:%s=%#x%s" % (norm(s.name), s.val, "%+d" % add if add else "")
        if s.shndx >= len(self.secs):
            return "special:%s" % s.name
        return self.at(s.shndx, s.val + add)

    def at(self, shndx, addr):
        k = self.kind[shndx]
        sec = self.secs[shndx]
        if k == "code":
            f = self.owner(self.fsyms, self.fval, shndx, addr)
            if f:
                return "fn:%s%s" % (norm(f.name), "+%d" % (addr - f.val) if addr != f.val else "")
            return "code:%s+%d" % (sec.name if sec.name in (".text", ".plt", ".init.plt") else "?", addr)
        if k == "str":
            return self.strat(shndx, addr, True)
        if k in ("data", "bss"):
            o = self.owner(self.osyms, self.oval, shndx, addr)
            lk = addr in self.zkeys.get(shndx, ())      # a zero-size lockdep key lives here: the merged stock section cannot tell it from the object at the same address
            en = self.ender(shndx, addr)                # an object ENDS exactly here (&array[N]): the pointer is the same symbol+addend whatever follows it in the section layout
            if o:
                d = "+%d" % (addr - o.val) if addr != o.val else ""
                if sec.flags & SHF_WRITE or sec.type == 8:
                    r = "obj:%s%s" % (norm(o.name), d)
                else:
                    st = self.conststr(shndx, o)
                    r = ("str:" + st[addr - o.val:]) if st else "ro:%s%s" % (self.blob(shndx, o.val, o.val + o.size), d)
                if en is not None and en is not o:
                    r += ALT + "end:" + norm(en.name)
                return r + ALT + "lockkey" if lk else r
            if lk:
                return "lockkey"
            if sec.type == 8:
                return "bss-anon" + (ALT + "end:" + norm(en.name) if en is not None else "")
            s = self.strat(shndx, addr, False)
            if s is not None:
                return s
            b = self.blob(shndx, addr, self.next_boundary(shndx, addr))
            if b != "zero":
                self.anon.setdefault((shndx, addr), b)
            self.aninfo.setdefault(b, "%s+%#x %s" % (sec.name, addr, self.d[sec.off + addr:sec.off + addr + 16].hex()))
            return "zero" if b == "zero" else "anon:" + b
        return "%s+%d" % (sec.name, addr)

    def conststr(self, shndx, o):
        """content of a named const object that is exactly one NUL-terminated string (e.g. static const char s[] = "abc"), else None"""
        c = self.d[self.secs[shndx].off + o.val:self.secs[shndx].off + o.val + o.size]
        if len(c) > 1 and c[-1] == 0 and STRCH.issuperset(c[:-1]):
            return c[:-1].decode("latin-1")
        return None

    def strat(self, shndx, addr, force):
        sec = self.secs[shndx]
        data = self.d[sec.off:sec.off + sec.size]
        j = data.find(b"\0", addr)
        if j < 0:
            return None if not force else "str:" + data[addr:].decode("latin-1")
        s = data[addr:j]
        if force:
            return "str:" + s.decode("latin-1") if s else "zero"
        if not s or not STRCH.issuperset(s):
            return None
        ov = self.oval.get(shndx, [])
        i = bisect.bisect_right(ov, addr)
        if i < len(ov) and ov[i] <= j:    # a named object starts inside the would-be string: it is data, not a string
            return None
        return "str:" + s.decode("latin-1")

    # ---- extraction
    def functions(self):
        out, seen = {}, collections.Counter()
        for sh in sorted(self.fsyms):
            for s in self.fsyms[sh]:
                if self.kind[sh] != "code":
                    continue
                key = norm(s.name)
                seen[key] += 1
                if seen[key] > 1:
                    key += "#%d" % seen[key]
                sec = self.secs[sh]
                code = self.d[sec.off + s.val:sec.off + s.val + s.size]
                ro = self.reloff.get(sh, [])
                rl = self.rel.get(sh, [])
                rel = [(rl[j][0] - s.val, rl[j][1], self.tgt(rl[j][2], rl[j][3]))
                       for j in range(bisect.bisect_left(ro, s.val), bisect.bisect_left(ro, s.val + s.size))]
                out[key] = (code, rel, self.preamble(sh, s.val), s.bind)
        return out

    def preamble(self, sh, val):
        """kCFI type hash word before the function symbol, only when a $d mapping symbol covers it"""
        if val < 4:
            return None
        mv = self.mval.get(sh)
        if not mv:
            return None
        i = bisect.bisect_right(mv, val - 4) - 1
        if i < 0 or self.maps[sh][i][1] != "d" or (i + 1 < len(mv) and mv[i + 1] < val):
            return None
        return struct.unpack_from("<I", self.d, self.secs[sh].off + val - 4)[0]

    def gaps(self):
        """executable bytes owned by no sized function symbol (asm stubs without .size, literal data): zero words, nops and kCFI preambles dropped"""
        out = collections.Counter()
        alt = collections.defaultdict(set)      # alternative replacement instructions sit in .text in the stock (placement differs, compared in .altinstructions)
        for i in self.secidx.get(".altinstructions", []):
            data = self.sdata(i)
            for off, t, sy, add in self.rel.get(i, []):
                if off % 12 == 4:
                    for o in range(self.syms[sy].val + add, self.syms[sy].val + add + data[off // 12 * 12 + 11], 4):
                        alt[self.syms[sy].shndx].add(o)
        for sh, sec in enumerate(self.secs):
            if self.kind[sh] != "code" or sec.type == 8 or not sec.size:
                continue
            data = self.sdata(sh)
            cover = sorted((x.val, x.val + x.size) for x in self.fsyms.get(sh, []))
            pos, spans = 0, []
            for lo, hi in cover:
                if lo > pos:
                    spans.append((pos, lo - 4 if lo - pos >= 4 and self.preamble(sh, lo) is not None else lo))
                pos = max(pos, hi)
            spans.append((pos, len(data)))
            ro, rl = self.reloff.get(sh, []), self.rel.get(sh, [])
            for lo, hi in spans:
                words = [data[o:o + 4] for o in range(lo, hi, 4) if o not in alt[sh]]
                if any(w not in (bytes(4), struct.pack("<I", 0xd503201f)) for w in words):
                    rr = tuple((rl[j][0] - lo, rl[j][1], self.tgtp(rl[j][2], rl[j][3])) for j in range(bisect.bisect_left(ro, lo), bisect.bisect_left(ro, hi)))
                    out[(sec.name if sec.name in (".plt", ".init.plt", ".text.ftrace_trampoline") else "?", data[lo:hi].hex(), rr)] += 1
        return out

    def objects(self):
        out = collections.defaultdict(list)
        for sh in sorted(self.osyms):
            if self.kind[sh] not in ("data", "bss"):
                continue
            sec = self.secs[sh]
            for s in self.osyms[sh]:
                if s.name.startswith(("$", ".L")):
                    continue
                if not sec.flags & SHF_WRITE and sec.type != 8 and self.conststr(sh, s):    # string constants: compared by content where referenced
                    self.nstr += 1
                    continue
                out[norm(s.name)].append((cls(sec.name, sec.flags, sec.type), s.size, self.blob(sh, s.val, s.val + s.size)))
        return out

    def bug_lines(self):
        """multiset of (function+offset, line) of __bug_table"""
        out = collections.Counter()
        for i in self.secidx.get("__bug_table", []):
            data = self.sdata(i)
            byoff = {r[0]: self.tgtp(r[2], r[3]) for r in self.rel.get(i, [])}
            for b in range(0, len(data) - 11, 12):
                out[(byoff.get(b, "?"), struct.unpack_from("<H", data, b + 8)[0])] += 1
        return out

    def table(self, name, size=None, norm_bug=False):
        cnt = collections.Counter()
        for i in self.secidx.get(name, []):
            sec = self.secs[i]
            data = self.sdata(i)
            sz = size or sec.entsize or 8
            ro = self.reloff.get(i, [])
            rl = self.rel.get(i, [])
            n = len(data) - len(data) % sz
            if n != len(data):
                cnt[("tail", data[n:].hex())] += 1
            for base in range(0, n, sz):
                b = bytearray(data[base:base + sz])
                r = []
                for j in range(bisect.bisect_left(ro, base), bisect.bisect_left(ro, base + sz)):
                    off, t, sy, add = rl[j]
                    f = FIELD.get(t, 4)
                    b[off - base:off - base + f] = bytes(len(b[off - base:off - base + f]))
                    r.append((off - base, t, self.tgtp(sy, add)))
                if name == ".altinstructions" and len(r) >= 2:     # replacement: compare its BYTES (placement differs between builds)
                    for k, (o, t, x) in enumerate(r):
                        if o == 4:
                            s2 = self.syms[rl[bisect.bisect_left(ro, base) + k][2]]
                            if 0 < s2.shndx < len(self.secs):
                                a = s2.val + rl[bisect.bisect_left(ro, base) + k][3]
                                alen = data[base + 11]
                                r[k] = (o, t, "altrepl:" + self.d[self.secs[s2.shndx].off + a:self.secs[s2.shndx].off + a + alen].hex())
                if norm_bug and name == "__bug_table":      # file path = build directory artefact (basename compared), line compared in bug-lines
                    b[8:10] = bytes(2)
                    r = [(o, t, "str:" + os.path.basename(x[4:]) if x.startswith("str:") else x) for o, t, x in r]
                cnt[(bytes(b).hex(), tuple(r))] += 1
        return cnt

    def exports(self):
        out = {}
        for ks in ("__ksymtab", "__ksymtab_gpl"):
            kc = "__kcrctab" + ks[9:]
            ents, crcs = [], b"".join(self.sdata(i) for i in self.secidx.get(kc, []))
            for i in self.secidx.get(ks, []):
                data, rl = self.sdata(i), self.rel.get(i, [])
                byoff = {}
                for off, t, sy, add in rl:
                    byoff[off] = self.tgtp(sy, add)
                ents += [(byoff.get(b, "?"), byoff.get(b + 4, "?"), byoff.get(b + 8, "")) for b in range(0, len(data), 12)]
            for k, (val, nm, ns) in enumerate(ents):
                crc = struct.unpack_from("<I", crcs, 4 * k)[0] if 4 * k + 4 <= len(crcs) else None
                out[nm[4:] if nm.startswith("str:") else nm] = (ks, crc, val, ns)
        return out

    def versions(self):
        out = {}
        for i in self.secidx.get("__versions", []):
            d = self.sdata(i)
            for o in range(0, len(d) - 63, 64):
                out[d[o + 8:d.index(b"\0", o + 8)].decode()] = struct.unpack_from("<Q", d, o)[0]
        return out

    def modinfo(self):
        out = []
        for i in self.secidx.get(".modinfo", []):
            out += [x.decode("latin-1") for x in self.sdata(i).split(b"\0") if x]
        return out

    def notes(self):
        r = {}
        for n, idx in self.secidx.items():
            if n.startswith(".note") and n != ".note.gnu.build-id":
                r[n] = b"".join(self.sdata(i) for i in idx).hex()
        return r


# ------------------------------------------------------------------ checks
def short(lst, v, n=8):
    lst = list(lst)
    return ", ".join(map(str, lst if v else lst[:n])) + ("" if v or len(lst) <= n else " ... (+%d)" % (len(lst) - n))


def releq(x, y):
    """relocation lists equal; a target with alternative readings (ALT) matches if any reading matches"""
    if x == y:
        return True
    if len(x) != len(y):
        return False
    for (o1, t1, d1), (o2, t2, d2) in zip(x, y):
        if o1 != o2 or t1 != t2 or (d1 != d2 and not set(d1.split(ALT)) & set(d2.split(ALT))):
            return False
    return True


def run(stock, built, v=False, exmap=None):
    a, b = Elf(stock), Elf(built)
    res = []   # (name, ok, summary, details)

    def add(name, ok, summ, det=()):
        res.append((name, ok, summ, list(det)))

    fa, fb = a.functions(), b.functions()
    so, bo = sorted(set(fa) - set(fb)), sorted(set(fb) - set(fa))
    add("function-set", not so and not bo, "%d functions" % len(fa) if not so and not bo else "stock %d / built %d; only stock: [%s]; only built: [%s]" % (len(fa), len(fb), short(so, v, 6), short(bo, v, 6)))
    com = [n for n in fa if n in fb]
    bad = [n for n in com if fa[n][0] != fb[n][0]]
    add("function-bytes", not bad, "all %d identical" % len(com) if not bad else "%d of %d differ: %s" % (len(bad), len(com), short(bad, v)))
    badr, det = [], []
    for n in com:
        if not releq(fa[n][1], fb[n][1]):
            badr.append(n)
            sa, sb = set(fa[n][1]), set(fb[n][1])
            x, y = sorted(sa - sb), sorted(sb - sa)
            det.append("%s: stock-only %s | built-only %s" % (n, x[:3], y[:3]))
    add("function-relocs", not badr, "all %d functions, %d relocations identical" % (len(com), sum(len(fa[n][1]) for n in com)) if not badr
        else "%d of %d differ: %s" % (len(badr), len(com), short(badr, v)), det if v else det[:4])
    bb = ["%s(stock %s built %s)" % (n, "GLOBAL" if fa[n][3] else "local", "GLOBAL" if fb[n][3] else "local") for n in com if (fa[n][3] != 0) != (fb[n][3] != 0)]
    add("function-binding", not bb, "all %d functions: same global/local binding" % len(com) if not bb else "%d differ (an extra global symbol / kCFI preamble): %s" % (len(bb), short(bb, v, 6)))
    pa = {n: fa[n][2] for n in com if fa[n][2] is not None or fb[n][2] is not None}
    badp = [n for n in pa if fa[n][2] != fb[n][2]]
    add("kcfi-preamble", not badp, "%d preamble words identical" % len(pa) if not badp else "%d of %d differ: %s" % (len(badp), len(pa), short(
        ["%s(stock %s built %s)" % (n, "none" if fa[n][2] is None else hex(fa[n][2]), "none" if fb[n][2] is None else hex(fb[n][2])) for n in badp], v, 4)))
    ga, gb = a.gaps(), b.gaps()
    add("unowned-code", ga == gb, "%d code spans outside function symbols" % sum(ga.values()) if ga == gb else "stock %d / built %d spans differ: %s | %s" % (
        sum(ga.values()), sum(gb.values()), [k[1][:40] for k in (ga - gb)][:2], [k[1][:40] for k in (gb - ga)][:2]))
    oa, ob = a.objects(), b.objects()
    ono, onb = sorted(set(oa) - set(ob)), sorted(set(ob) - set(oa))
    sigs = lambda e, names: collections.Counter(x for k in names for x in e[k])
    renamed = ono and onb and sigs(oa, ono) == sigs(ob, onb)      # same (class, size, content) set under other names: symbol naming only
    if renamed:
        ono, onb = [], []
    odiff = [k for k in oa if k in ob and collections.Counter(oa[k]) != collections.Counter(ob[k])]
    det = ["%s: stock %s | built %s" % (k, oa[k], ob[k]) for k in odiff]
    nobj = sum(len(x) for x in oa.values())
    add("objects", not (ono or onb or odiff), "%d named objects (global+static, content+relocs)%s%s" % (nobj, "; %d const strings via references" % a.nstr if a.nstr else "", "; some statics only renamed" if renamed else "") if not (ono or onb or odiff)
        else "stock %d / built %d; only stock [%s]; only built [%s]; content/size/class differ [%s]" % (nobj, sum(len(x) for x in ob.values()), short(ono, v, 6), short(onb, v, 6), short(odiff, v, 6)), det if v else det[:4])
    for e in (a, b):                       # make sure EVERY relocation was resolved (data + tables too)
        for si_, lst in e.rel.items():
            if e.kind[si_] != "meta":
                for off, t, sy, add_ in lst:
                    e.tgt(sy, add_)
    ca, cb = collections.Counter(a.anon.values()), collections.Counter(b.anon.values())
    det = ["stock-only %s" % a.aninfo[h] for h in (ca - cb).elements()] + ["built-only %s" % b.aninfo[h] for h in (cb - ca).elements()]
    add("anon-data", ca == cb, "%d anonymous chunks (content)" % sum(ca.values()) if ca == cb else "stock %d / built %d chunks; stock-only %d, built-only %d" % (
        sum(ca.values()), sum(cb.values()), sum((ca - cb).values()), sum((cb - ca).values())), det if v else det[:3])
    for chk, names in (("bug-table", ["__bug_table"]), ("bug-lines", ["__bug_table"]), ("jump-table", ["__jump_table"]), ("other-tables", None)):
        if chk == "bug-lines":     # line numbers inside the BUG/WARN entries (differ when the kernel headers differ): cosmetic, reported separately
            la, lb = a.bug_lines(), b.bug_lines()
            add(chk, la == lb, "%d entries, line numbers identical" % sum(la.values()) if la == lb else "%d of %d line numbers differ (e.g. %s | %s)" % (
                sum((la - lb).values()), sum(la.values()), list((la - lb).elements())[:2], list((lb - la).elements())[:2]))
            continue
        if names is None:
            names = sorted(({n for n in a.secidx} | {n for n in b.secidx}) - {"__bug_table", "__jump_table"})
            names = [n for n in names if any(e.kind[i] == "table" for e in (a, b) for i in e.secidx.get(n, []))]
        sd, dd, tot = [], [], 0
        for n in names:
            ta, tb = a.table(n, TABLES.get(n), True), b.table(n, TABLES.get(n), True)
            tot += sum(ta.values())
            if ta != tb:
                sd.append("%s(stock %d/built %d)" % (n, sum(ta.values()), sum(tb.values())))
                dd += ["%s stock-only %s" % (n, k) for k in list((ta - tb).elements())[:2]] + ["%s built-only %s" % (n, k) for k in list((tb - ta).elements())[:2]]
        add(chk, not sd, "%d entries identical" % tot if not sd else "differ: " + ", ".join(sd), dd if v else dd[:3])
    ea, eb = a.exports(), b.exports()
    ed = sorted(k for k in set(ea) | set(eb) if ea.get(k) != eb.get(k))
    add("exports", not ed, "%d exports + CRC + kind + target" % len(ea) if not ed else "%d of %d differ: %s" % (len(ed), len(set(ea) | set(eb)), short(
        ["%s(stock %s built %s)" % (k, ea.get(k, "-")[1:2] if k in ea else "-", eb.get(k, "-")[1:2] if k in eb else "-") for k in ed], v, 4)))
    va, vb = a.versions(), b.versions()
    vd = sorted(k for k in set(va) | set(vb) if va.get(k) != vb.get(k))
    add("imports", not vd, "%d imports + CRC" % len(va) if not vd else "%d of %d differ: %s" % (len(vd), len(set(va) | set(vb)), short(
        ["%s(stock %s built %s)" % (k, hex(va[k]) if k in va else "-", hex(vb[k]) if k in vb else "-") for k in vd], v, 4)))
    ma, mb = a.modinfo(), b.modinfo()
    META = ("depends=", "vermagic=", "intree=", "scmversion=", "built_with=", "srcversion=", "staging=")   # separate checks / build-system tags
    key = lambda l, pre: sorted(x for x in l if not x.startswith(pre))
    mia, mib = sorted(set(key(ma, META))), sorted(set(key(mb, META)))       # a tag repeated by two merged translation units (license=GPL twice) is not a difference
    ca_, cb_ = collections.Counter(mia), collections.Counter(mib)
    dm = sorted(set((ca_ - cb_).elements()) | set((cb_ - ca_).elements())) if mia != mib else []
    add("modinfo", mia == mib, "%d distinct tags (license, parm, alias, name, ...)" % len(mia) if mia == mib else "differ: " + short(
        ["stock:" + x if ca_[x] > cb_[x] else "built:" + x for x in dm], v, 5))
    bma, bmb = sorted(x for x in ma if x.startswith(META[2:])), sorted(x for x in mb if x.startswith(META[2:]))
    add("buildmeta", bma == bmb, "intree/scmversion/built_with/srcversion identical" if bma == bmb else "stock %s vs built %s (note: no intree= taints the kernel as out-of-tree)" % (bma, bmb))
    g = lambda l, k: next((x[len(k):] for x in l if x.startswith(k)), None)
    da, db = g(ma, "depends="), g(mb, "depends=")
    add("depends", da == db, "depends=%s" % da if da == db else "stock '%s' vs built '%s'" % (da, db))
    vma, vmb = g(ma, "vermagic="), g(mb, "vermagic=")
    fl = lambda s: s.split(" ", 1)[1] if s and " " in s else s
    add("vermagic", fl(vma) == fl(vmb), "flags '%s'" % fl(vma) if fl(vma) == fl(vmb) else "stock '%s' vs built '%s'" % (vma, vmb))
    na, nb = a.notes(), b.notes()
    add("notes", na == nb, "%d note sections (build-id ignored)" % len(na) if na == nb else "differ: " + ", ".join(sorted(k for k in set(na) | set(nb) if na.get(k) != nb.get(k))))
    # INFO: imports resolved through forged providers
    ds = set((da or "").split(",")) - {""}
    dbs = set((db or "").split(",")) - {""}
    forged = sorted(dbs - ds)
    gone = ds - dbs
    info = ""
    if forged:
        info = "depends has %d provider(s) the stock does not (forged/stub: %s); stock providers replaced: %s" % (len(forged), ", ".join(forged), ", ".join(sorted(gone)) or "-")
        if exmap:
            fi = sorted(k for k in vb if exmap.get(k) in gone)
            info += "; %d imports resolve to them, CRC match does NOT prove the prototype: %s" % (len(fi), short(fi, v, 10))
    return a, b, res, info


def load_map(p):
    m = {}
    if p and os.path.exists(p):
        for l in open(p):
            f = l.rstrip("\n").split("\t")
            if len(f) >= 3:
                m.setdefault(f[0], f[2])
    return m


def build_map(d, out):
    with open(out, "w") as o:
        for r, _, fs in os.walk(d):
            for f in sorted(fs):
                if f.endswith(".ko"):
                    try:
                        e = Elf(os.path.join(r, f))
                    except Exception:
                        continue
                    for k, (ks, crc, val, ns) in e.exports().items():
                        o.write("%s\t%#x\t%s\n" % (k, crc or 0, f[:-3]))


def report(stock, built, v, exmap):
    a, b, res, info = run(stock, built, v, exmap)
    for n, ok, s, det in res:
        print("%-4s %-14s %s" % ("PASS" if ok else "FAIL", n, s))
        for x in det:
            print("       " + x[:300 if not v else 2000])
    if info:
        print("INFO %-14s %s" % ("forged-imports", info))
    rel = lambda e: next((x[9:].split(" ")[0] for x in e.modinfo() if x.startswith("vermagic=")), "")
    if rel(a) != rel(b):
        print("INFO %-14s release stock %s built %s (ignored by same_magic with MODVERSIONS)" % ("vermagic-rel", rel(a), rel(b)))
    fails = [n for n, ok, *_ in res if not ok]
    print("VERDICT: " + ("STRICT-EQUIVALENT" if not fails else "NOT STRICT-EQUIVALENT (failing: %s)" % ", ".join(fails)))
    return fails


if __name__ == "__main__":
    args = [x for x in sys.argv[1:]]
    v = "-v" in args
    args = [x for x in args if x != "-v"]
    exmap = None
    if "--exports-map" in args:
        i = args.index("--exports-map")
        exmap = load_map(args[i + 1])
        del args[i:i + 2]
    if args and args[0] == "--build-map":
        build_map(args[1], args[2])
    elif args and args[0] == "--batch":
        csvout = args[args.index("--csv") + 1] if "--csv" in args else None
        rows = []
        for l in open(args[1]):
            f = l.rstrip("\n").split("\t")
            if len(f) < 3:
                continue
            try:
                a, b, res, info = run(f[1], f[2], False, exmap)
                fails = [n for n, ok, *_ in res if not ok]
                rows.append((f[0], f[2], "STRICT-EQUIVALENT" if not fails else "NOT", ",".join(fails), info, " || ".join("%s: %s" % (n, s) for n, ok, s, d in res if not ok)))
            except Exception as ex:
                rows.append((f[0], f[2], "ERROR", "", "", repr(ex)))
            print("\t".join(rows[-1][:4]), flush=True)
        if csvout:
            import csv
            with open(csvout, "w", newline="") as o:
                w = csv.writer(o)
                w.writerow(["module", "built", "verdict", "failing", "forged_info", "details"])
                w.writerows(rows)
    else:
        sys.exit(1 if report(args[0], args[1], v, exmap) else 0)
