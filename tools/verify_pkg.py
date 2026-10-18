#!/usr/bin/env python3
"""Static verification of a dada kernel-module package (no hardware needed). Runs on the build server (needs only python3).

Usage:
  verify_pkg.py <root> [--symvers FILE] [--protected FILE] [--stock DIR] [--vmlinux FILE] [--csv OUT.csv] [--quiet]

<root> holds the OS4 layout: <root>/{vendor_ramdisk,vendor_dlkm,system_dlkm}/*.ko (system_dlkm may have subdirs) and the
load lists <root>/modules.load (vendor_boot) and <root>/vendor_dlkm.modules.load.  --stock (default
<work-dir>) is the OS4 stock set; for every module whose bytes differ from stock, modinfo `depends` must equal
the stock one (a stub provider name there would break modprobe).

Checks (per module):
  vermagic   flag part (after the release string) equals what the Image expects ("SMP preempt mod_unload modversions aarch64").
             With CONFIG_MODVERSIONS=y same_magic() ignores the release string, so 6.6.57/6.6.118 vs 6.6.144 is only informational.
  crc        every __versions entry exists in Module.symvers (vmlinux or a GKI module) or in an export of another package
             module, with the same CRC.
  export     no exported symbol collides with the kernel or with another module of the set.
  depends    modinfo depends names exist in the set; every provider of an imported symbol is in `depends`.
  protected  unsigned module must not import a CONFIG_MODULE_SIG_PROTECT protected export (abi_gki_protected_exports_aarch64).
  kcfi       module is kCFI-instrumented (ldur w16,[xN,#-4] checks + brk #0x82xx traps) and exported/init functions have a
             type-hash preamble word (not an instruction).
  order      load order of modules.load / vendor_dlkm.modules.load: every dependency (depends or symbol provider) loads
             earlier (or is a GKI/system_dlkm module or not in any list = reported as 'dep-not-listed').
  hook       (optional, --hook) kCFI hash of a registered tracepoint probe equals the hash the vmlinux traceiter expects.
  strict     (G9, default on when compare_ko_v2.py is found) every file that differs from the OS4 stock is compared with compare_ko_v2.py (function bytes + relocations +
             binding, kCFI preambles, ALL named objects incl. .data/.rodata/.bss content, anonymous data, bug/jump/other tables, exports/imports + CRC, modinfo, depends,
             notes). Classes: STRICT (no difference), metadata-only (only buildmeta/depends/notes/vermagic), cosmetic (+ bug-table line numbers), crc-hack (only import/export CRC
             differences that crc_patch/forged stubs explain), REAL (anything else). A REAL module counts as a problem: it must be replaced by the stock file.
"""
import argparse, collections, csv, os, re, struct, subprocess, sys
from collections import defaultdict

FLAGS_EXPECTED = "SMP preempt mod_unload modversions aarch64"


class Ko:
    def __init__(self, path):
        self.path = path
        d = self.d = open(path, "rb").read()
        self.signed = d.endswith(b"~Module signature appended~\n")
        shoff, = struct.unpack_from("<Q", d, 0x28)
        es, n, si = struct.unpack_from("<HHH", d, 0x3A)
        raw = [struct.unpack_from("<IIQQQQII", d, shoff + i * es) for i in range(n)]
        so = raw[si][4]
        nm = lambda o: d[so + o:d.index(b"\0", so + o)].decode()
        self.snames = [nm(r[0]) for r in raw]
        self.sec = {nm(r[0]): r for r in raw}
        self.raw = raw
        self.modinfo = {}
        if ".modinfo" in self.sec:
            r = self.sec[".modinfo"]
            for s in d[r[4]:r[4] + r[5]].split(b"\0"):
                if b"=" in s:
                    k, v = s.decode(errors="replace").split("=", 1)
                    self.modinfo.setdefault(k, v)
        self.name = self.modinfo.get("name", os.path.basename(path)[:-3]).replace("-", "_")
        self._syms = None

    def versions(self):
        r = self.sec.get("__versions")
        if not r:
            return []
        return [(self.d[o + 8:self.d.index(b"\0", o + 8)].decode(), struct.unpack_from("<Q", self.d, o)[0])
                for o in range(r[4], r[4] + r[5], 64)]

    def symtab(self):
        if self._syms is None:
            r = self.sec[".symtab"]
            stro = self.raw[r[6]][4]
            out = []
            for o in range(r[4], r[4] + r[5], 24):
                nm, info, _o, shndx, val, size = struct.unpack_from("<IBBHQQ", self.d, o)
                out.append((self.d[stro + nm:self.d.index(b"\0", stro + nm)].decode(), info & 15, info >> 4, shndx, val, size))
            self._syms = out
        return self._syms

    def exports(self):
        """name -> crc, via __ksymtab[_gpl] (12-byte entries) paired with __kcrctab[_gpl] (4-byte entries)."""
        order = self.snames
        out = {}
        kc = {}
        for k in ("__ksymtab", "__ksymtab_gpl"):
            c = "__kcrctab" + k[len("__ksymtab"):]
            if k in self.sec and c in self.sec:
                kc[order.index(k)] = self.sec[c][4]
        if not kc:
            return out
        for name, _t, _b, shndx, val, _sz in self.symtab():
            if shndx in kc and name.startswith("__ksymtab_"):
                out[name[10:]] = struct.unpack_from("<I", self.d, kc[shndx] + val // 12 * 4)[0]
        return out

    def text_words(self):
        for s, r in zip(self.snames, self.raw):
            if s.startswith((".text", ".init.text", ".exit.text")) and r[1] == 1:
                yield s, r, self.d[r[4]:r[4] + r[5]]

    def kcfi(self):
        """(instrumented, n_global_funcs, n_with_preamble) ; preamble word = not ret/b/nop/pac instruction."""
        chk = trap = 0
        for _s, _r, b in self.text_words():
            for (w,) in struct.iter_unpack("<I", b[:len(b) // 4 * 4]):
                if (w & 0xFFFFFC1F) == 0xb85fc010:
                    chk += 1
                elif (w & 0xFFE0001F) == 0xd4200000 and 0x8200 <= ((w >> 5) & 0xFFFF) <= 0x83ff:
                    trap += 1
        order = self.snames
        textidx = {i: s for i, s in enumerate(order) if s.startswith((".text", ".init.text", ".exit.text"))}
        nf = npre = 0
        for name, typ, bind, shndx, val, _sz in self.symtab():
            if typ == 2 and bind == 1 and shndx in textidx and val >= 4:
                r = self.raw[shndx]
                w, = struct.unpack_from("<I", self.d, r[4] + val - 4)
                nf += 1
                if w not in (0xd65f03c0, 0xd503201f, 0xd503233f, 0xd50323bf) and (w >> 26) not in (0x05, 0x25):
                    npre += 1
        return (chk > 0 and trap > 0), nf, npre

    def signer(self):
        """serial (hex) of the PKCS#7 signer of an appended module signature, None if unsigned. Needs `openssl` (server has it)."""
        if not self.signed:
            return None
        d = self.d[:-len(b"~Module signature appended~\n")]
        siglen, = struct.unpack(">I", d[-4:])
        pk = d[-12 - siglen:-12]
        r = subprocess.run(["openssl", "asn1parse", "-inform", "DER", "-i"], input=pk, capture_output=True)
        ser = [m.group(1) for m in re.finditer(r"INTEGER\s+:([0-9A-F]{30,})", r.stdout.decode())]
        return ser[-1] if ser else "?"

    def all_preambles(self):
        out = set()
        for name, typ, bind, shndx, val, _sz in self.symtab():
            if typ == 2 and val >= 4 and self.snames[shndx].startswith((".text", ".init.text", ".exit.text")):
                out.add(struct.unpack_from("<I", self.d, self.raw[shndx][4] + val - 4)[0])
        return out

    def hash_before(self, fn):
        for name, typ, bind, shndx, val, _sz in self.symtab():
            if name == fn and typ == 2:
                r = self.raw[shndx]
                return struct.unpack_from("<I", self.d, r[4] + val - 4)[0]
        return None


def preambles(k):
    """{function: kCFI hash word in the 4 bytes BEFORE the symbol} for functions that carry a preamble: GLOBAL ones and local ones whose address is taken
    (a non-branch relocation points at them). Static functions that are only called directly have no preamble (the word is the previous instruction).
    compare_ko.py does not look at these words."""
    sym = k.symtab()
    taken = set()
    byshndx = collections.defaultdict(list)
    for name, typ, bind, shndx, val, sz in sym:
        if typ == 2:
            byshndx[shndx].append((val, sz, name))
    for r in k.raw:
        if r[1] != 4:
            continue
        for o in range(r[4], r[4] + r[5], 24):
            _off, info, add = struct.unpack_from("<QQq", k.d, o)
            if (info & 0xFFFFFFFF) in (282, 283):  # JUMP26 / CALL26
                continue
            name, typ, bind, shndx, val, sz = sym[info >> 32]
            if typ == 2:
                taken.add(name)
            elif typ == 3:
                for v, z, nm in byshndx.get(shndx, []):
                    if v <= add < v + max(z, 1):
                        taken.add(nm)
    out = {}
    for name, typ, bind, shndx, val, _sz in sym:
        # ".llvm." = ThinLTO-promoted statics that the sources define as hidden globals (documented hack): stock has no preamble there, ours emits one (harmless)
        if typ == 2 and ".llvm." not in name and val >= 4 and k.snames[shndx].startswith((".text", ".init.text", ".exit.text")) and (bind == 1 or name in taken):
            out[name] = struct.unpack_from("<I", k.d, k.raw[shndx][4] + val - 4)[0]
    return out


def load_symvers(p):
    kern = {}  # name -> (crc, module, export type)
    for line in open(p):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 3:
            kern[f[1]] = (int(f[0], 16), f[2], f[3] if len(f) > 3 else "")
    return kern


def collect(root):
    mods = []
    for part in ("vendor_ramdisk", "vendor_dlkm", "system_dlkm"):
        for dp, _dn, fn in os.walk(os.path.join(root, part)):
            for f in sorted(fn):
                if f.endswith(".ko"):
                    mods.append((part, os.path.join(dp, f)))
    return mods


def read_list(p):
    if not p or not os.path.exists(p):
        return []
    return [os.path.basename(l.strip())[:-3].replace("-", "_") for l in open(p) if l.strip().endswith(".ko")]


def read_dep(p):
    """modules.dep -> {module: [deps]} (names without path/.ko)."""
    out = {}
    if not p or not os.path.exists(p):
        return out
    nm = lambda x: os.path.basename(x)[:-3].replace("-", "_")
    for l in open(p):
        if ":" in l:
            a, b = l.split(":", 1)
            out[nm(a.strip())] = [nm(x) for x in b.split()]
    return out


def traceiter_hash(vmlinux, symbol, objdump):
    """kCFI hash the vmlinux expects at the indirect call in <symbol>: `movk w17,#lo ; movk w17,#hi,lsl #16` (or mov w17,#lo)."""
    txt = subprocess.check_output([objdump, "-d", "--no-show-raw-insn", f"--disassemble-symbols={symbol}", vmlinux], text=True)
    lo, vals = None, []
    for l in txt.splitlines():
        m = re.search(r"mov[k]?\s+w17, #(0x[0-9a-f]+|\d+)$", l)
        if m:
            lo = int(m.group(1), 0)
            continue
        m = re.search(r"movk\s+w17, #(0x[0-9a-f]+|\d+), lsl #16", l)
        if m and lo is not None:
            vals.append(int(m.group(1), 0) << 16 | lo)
            lo = None
    return sorted(set(vals))


def traceiter_hashes(vmlinux, symbols, objdump):
    """{symbol: set(expected kCFI hashes)} for many __traceiter_* functions with ONE objdump run."""
    cmd = [objdump, "-d", "--no-show-raw-insn"] + [f"--disassemble-symbols={x}" for x in symbols] + [vmlinux]
    txt = subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL)
    out, cur, lo = {}, None, None
    for l in txt.splitlines():
        m = re.match(r"^[0-9a-f]{16} <([^>]+)>:$", l)
        if m:
            cur, lo = m.group(1), None
            out[cur] = set()
            continue
        m = re.search(r"mov[k]?\s+w17, #(0x[0-9a-f]+|\d+)$", l)
        if m:
            lo = int(m.group(1), 0)
            continue
        m = re.search(r"movk\s+w17, #(0x[0-9a-f]+|\d+), lsl #16", l)
        if m and lo is not None and cur:
            out[cur].add(int(m.group(1), 0) << 16 | lo)
            lo = None
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--symvers", default="<build-dir>")
    ap.add_argument("--protected", default="<build-dir>")
    ap.add_argument("--kmi", default="<build-dir>")
    ap.add_argument("--ikconfig", default="<work-dir>")
    ap.add_argument("--lists", help="dir with vendor_boot.modules.{load,dep}, vendor_dlkm.modules.load, system_dlkm.modules.load (default <root>/lists)")
    ap.add_argument("--stock", default="<work-dir>")
    ap.add_argument("--csv")
    ap.add_argument("--vmlinux", default="<build-dir>")
    ap.add_argument("--objdump", default="<build-dir>")
    ap.add_argument("--hook", action="append", default=[], help="MOD:PROBE_FUNC:TRACEITER_SYMBOL")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--no-hooks", action="store_true")
    ap.add_argument("--trusted-ko", default="<build-dir>", help="a .ko signed by the kernel build key of the Image: its signer serial = the trusted key")
    ap.add_argument("--emitters", default="ufs_qcom,zram,zsmalloc,gzvm", help="modules that EMIT vendor hooks inline (ufs_qcom embeds the ufshcd core, zram/zsmalloc/gzvm call their own hooks): their __tracepoint_android_* imports are not probe registrations")
    ap.add_argument("--no-strict", action="store_true", help="skip the compare_ko_v2 strict check of every non-stock file")
    ap.add_argument("--strict-tool", default=None, help="path of compare_ko_v2.py (default: next to this script, then <work-dir>)")
    ap.add_argument("--exports-map", default="<work-dir>", help="exports map of compare_ko_v2 (optional)")
    a = ap.parse_args()
    lists = a.lists or os.path.join(a.root, "lists")

    kern = load_symvers(a.symvers)
    prot = {l.strip() for l in open(a.protected) if l.strip()}
    kmi = {l.strip() for l in open(a.kmi) if l.startswith(" ")} if os.path.exists(a.kmi) else set()
    relax = os.path.exists(a.ikconfig) and "CONFIG_APEX_RELAX_GKI_MODULE_KMI=y" in open(a.ikconfig).read()
    modname = lambda path: path.split("/")[-1].replace("-", "_")
    gki_mod = {modname(v[1]) for v in kern.values() if v[1] != "vmlinux"}
    mods = collect(a.root)
    trusted = None
    if os.path.exists(a.trusted_ko):
        trusted = Ko(a.trusted_ko).signer()  # a module signed by the key built into OUR Image (certs/signing_key of the kernel build)
    kos = {}
    parts = defaultdict(set)
    for part, p in mods:
        k = Ko(p)
        k.part = part
        kos.setdefault(k.name, k)  # same name in two partitions: first (ramdisk) wins; content mismatch is reported
        parts[part].add(k.name)
    dup_diff = 0
    byname = defaultdict(list)
    for part, p in mods:
        byname[os.path.basename(p)].append(p)
    for n, ps in byname.items():
        if len(ps) > 1 and len({open(p, "rb").read() for p in ps}) > 1:
            dup_diff += 1
            print("NOTE same module name with different content in two partitions:", n, [os.path.dirname(p).split("/")[-1] for p in ps])

    prov = {}  # symbol -> module name (package modules, incl. system_dlkm)
    exp_of = {n: k.exports() for n, k in kos.items()}
    for n, e in exp_of.items():
        for s in e:
            if s in prov and prov[s] != n:
                print("EXPORT-DUP", s, prov[s], n)
            prov[s] = n

    stock_dep, stock_path = {}, {}
    for dp, _dn, fn in os.walk(a.stock):
        for f in fn:
            if f.endswith(".ko"):
                try:
                    stock_dep.setdefault(f[:-3].replace("-", "_"), Ko(os.path.join(dp, f)).modinfo.get("depends", ""))
                    stock_path.setdefault(f[:-3].replace("-", "_"), os.path.join(dp, f))
                except Exception:
                    pass

    rows, depsets = [], {}
    for n, k in sorted(kos.items()):
        sg = k.signer() if k.signed else None
        k.sig_ok = bool(sg and sg == trusted)
        r = dict(module=n, part=k.part, signed=int(k.signed), sig_ok=int(k.sig_ok))
        rel, _, flags = k.modinfo.get("vermagic", "").partition(" ")
        r["release"] = rel
        r["vermagic_flags_ok"] = int(flags == FLAGS_EXPECTED)
        bad, miss, outside, providers = [], [], [], set()
        for s, c in k.versions():
            if s in kern:
                if kern[s][0] != c:
                    bad.append(s)
                if kern[s][1] != "vmlinux":
                    providers.add(modname(kern[s][1]))
                if s not in kmi and kern[s][1] == "vmlinux":
                    outside.append(s)
            elif s in prov:
                if exp_of[prov[s]].get(s) != c:
                    bad.append(s + "(pkg)")
                providers.add(prov[s])
            else:
                miss.append(s)
        pexp = [s for s in exp_of[n] if s in prot] if not k.sig_ok else []
        coll = [s for s in exp_of[n] if s in kern and modname(kern[s][1]) != n]
        r.update(crc_bad=len(bad), crc_missing=len(miss), protected_export_unsigned=len(pexp),
                 export_collide_kernel=len(coll), imports_outside_kmi=len(outside),
                 examples=";".join((bad + miss + pexp + coll)[:3]))
        depn = [x.replace("-", "_") for x in k.modinfo.get("depends", "").split(",") if x]
        r["depends"] = k.modinfo.get("depends", "")
        r["dep_missing"] = sum(1 for x in depn if x not in kos and x not in gki_mod)
        provmods = {x for x in providers if x != n}
        r["provider_not_in_depends"] = len([x for x in provmods if x not in depn and x not in gki_mod])
        sd = stock_dep.get(n)
        r["depends_vs_stock"] = "" if sd is None else ("same" if sd == k.modinfo.get("depends", "") else "DIFF(stock=%s)" % sd)
        r["preamble_vs_stock"] = ""
        if n in stock_path and not k.sig_ok and open(stock_path[n], "rb").read() != k.d:  # GKI modules of our own kernel build legitimately differ from the 6.6.118 stock
            pa, pb = preambles(Ko(stock_path[n])), preambles(k)
            r["preamble_vs_stock"] = sum(1 for x in pa if x in pb and pa[x] != pb[x])
        try:
            instr, nf, npre = k.kcfi()
        except KeyError:
            instr, nf, npre = False, 0, 0
        r["kcfi_instr"], r["fn_global"], r["fn_preamble"] = int(instr), nf, npre
        depsets[n] = set(depn) | provmods
        rows.append(r)

    # load simulation (init resolves dependencies like modprobe: deps first, any list order is fine); stage availability
    vb, vd, sd_ = (read_list(os.path.join(lists, f)) for f in ("vendor_boot.modules.load", "vendor_dlkm.modules.load", "system_dlkm.modules.load"))
    stock_moddep = read_dep(os.path.join(lists, "vendor_boot.modules.dep"))
    cyc, unavail, notinset = [], [], []

    def sim(lst, avail, label):
        done, stack = set(), []

        def load(m):
            if m in done:
                return
            if m in stack:
                cyc.append((label, m, list(stack)))
                return
            stack.append(m)
            for d in sorted(depsets.get(m, ())):
                if d in gki_mod and d not in kos:
                    continue
                if d not in avail:
                    unavail.append((label, m, d))
                else:
                    load(d)
            stack.pop()
            done.add(m)
        for m in lst:
            if m not in kos:
                notinset.append((label, m))
            else:
                load(m)
    ramdisk, dlkm, sysd = parts["vendor_ramdisk"], parts["vendor_dlkm"], parts["system_dlkm"]
    sim(vb, ramdisk | sysd, "vendor_boot")
    sim(vd, ramdisk | dlkm | sysd, "vendor_dlkm")
    sim(sd_, sysd | ramdisk, "system_dlkm")
    nodep = 0
    for m, dl in stock_moddep.items():
        if m in depsets and not depsets[m] <= set(dl):
            nodep += 1
            print("  MODULES.DEP does not cover", m, sorted(depsets[m] - set(dl)))

    # hook/tracepoint probe audit: every `__tracepoint_X` import (probe registration) needs a function in the module whose kCFI hash equals the
    # hash the vmlinux __traceiter_X indirect call checks. compare_ko.py does not compare these hash words.
    hook_bad, hook_total = [], 0
    rmap = {r["module"]: r for r in rows}
    if not a.no_hooks and os.path.exists(a.vmlinux):
        need = defaultdict(list)
        for n, k in kos.items():
            for sname, _c in k.versions():
                if sname.startswith("__tracepoint_"):
                    need[n].append(sname)
        allsyms = sorted({"__traceiter_" + x[len("__tracepoint_"):] for v in need.values() for x in v})
        exp = traceiter_hashes(a.vmlinux, allsyms, a.objdump) if allsyms else {}
        for n, lst in need.items():
            if n in a.emitters.split(","):
                continue
            hashes = kos[n].all_preambles()
            fnames = {nm for nm, typ, _b, _s, _v, _z in kos[n].symtab() if typ == 2}
            for x in lst:
                if "trace_" + x[len("__tracepoint_"):] in fnames:  # out-of-line emitter (ufs_qcom contains the ufshcd core, zram/zsmalloc call their own hooks)
                    continue
                e = exp.get("__traceiter_" + x[len("__tracepoint_"):])
                if not e:
                    continue
                # only Android vendor hooks count: plain tracepoints (mmap_lock_*, rwmmio_*, sk_data_ready...) are mostly EMITTED by inline code in the module
                if not x.startswith(("__tracepoint_android_vh_", "__tracepoint_android_rvh_")):
                    continue
                hook_total += 1
                if not (e & hashes):
                    hook_bad.append((n, x, sorted(hex(h) for h in e)))
                    rmap[n]["hook_mismatch"] = rmap[n].get("hook_mismatch", 0) + 1
    for r in rows:
        r.setdefault("hook_mismatch", 0)

    # strict check (compare_ko_v2) of every file that differs from the OS4 stock
    strict_rows, strict_note = [], ""
    for r in rows:
        r["strict"], r["strict_fail"] = "", ""
    if not a.no_strict:
        tool = a.strict_tool or next((t for t in (os.path.join(os.path.dirname(os.path.abspath(__file__)), "compare_ko_v2.py"), "<work-dir>") if os.path.exists(t)), None)
        if tool is None:
            strict_note = "strict check skipped: compare_ko_v2.py not found"
        else:
            sys.path.insert(0, os.path.dirname(tool))
            import compare_ko_v2 as v2
            exmap = v2.load_map(a.exports_map) if a.exports_map and os.path.exists(a.exports_map) else None
            META = {"depends", "buildmeta", "notes", "vermagic"}

            def klass(f):
                if not f:
                    return "STRICT"
                if f <= META:
                    return "metadata-only"
                if f <= META | {"bug-lines"}:
                    return "cosmetic"
                if f <= META | {"bug-lines", "imports", "exports"} and f & {"imports", "exports"}:
                    return "crc-hack"
                return "REAL"
            order = ["STRICT", "metadata-only", "cosmetic", "crc-hack", "REAL"]
            for part, p in mods:
                if part == "system_dlkm":      # GKI modules of the Image's own build: not comparable with the 6.6.118 stock
                    continue
                k = Ko(p)
                sp = os.path.join(a.stock, part, os.path.relpath(p, os.path.join(a.root, part)))
                if not os.path.exists(sp):
                    sp = stock_path.get(k.name)
                if not sp or open(sp, "rb").read() == k.d:
                    continue
                try:
                    _a, _b, res, _info = v2.run(sp, p, False, exmap)
                    f = {n for n, ok, *_ in res if not ok}
                    cl = klass(f)
                except Exception as ex:  # a module the tool cannot parse is not clean
                    f, cl = {"error:" + repr(ex)[:60]}, "REAL"
                strict_rows.append((k.name, part, cl, sorted(f)))
                rr = rmap[k.name]
                if not rr["strict"] or order.index(cl) > order.index(rr["strict"]):
                    rr["strict"], rr["strict_fail"] = cl, ",".join(sorted(f))

    hook_res = []
    for h in a.hook:
        m, fn, ti = h.split(":")
        want = traceiter_hash(a.vmlinux, ti, a.objdump)
        got = kos[m].hash_before(fn) if m in kos else None
        hook_res.append((h, got, want, got in want))

    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
    c = lambda key, cond: sum(1 for r in rows if cond(r[key]))
    print(f"modules: {len(rows)} unique ({len(mods)} files; {dup_diff} names differ between partitions), signed: {c('signed', bool)}, signature trusted by this Image: {c('sig_ok', bool)}; kernel CONFIG_APEX_RELAX_GKI_MODULE_KMI={'y' if relax else 'n/unknown'}")
    print(f"vermagic flags ok: {c('vermagic_flags_ok', bool)}/{len(rows)}; releases: " +
          ", ".join(f"{k}={v}" for k, v in sorted(__import__('collections').Counter(r['release'] for r in rows).items())))
    print(f"CRC mismatch vs Module.symvers/package: {c('crc_bad', bool)} modules; unresolved imports: {c('crc_missing', bool)} modules")
    print(f"unsigned module exports a protected symbol (-EACCES at load): {c('protected_export_unsigned', bool)}; exports colliding with another owner: {c('export_collide_kernel', bool)}")
    print(f"vmlinux imports outside the KMI list (loadable only because RELAX={'y' if relax else 'n'}): {c('imports_outside_kmi', bool)} modules, {sum(r['imports_outside_kmi'] for r in rows)} imports")
    print(f"depends names missing from set: {c('dep_missing', bool)}; symbol providers not in depends: {c('provider_not_in_depends', bool)}; depends differ from stock: {c('depends_vs_stock', lambda x: x.startswith('DIFF'))}")
    print(f"modules differing from stock: {sum(1 for r in rows if r['preamble_vs_stock'] != '')}; of them with kCFI preamble-hash mismatches vs stock: {c('preamble_vs_stock', lambda x: x not in ('', 0))}")
    print(f"kCFI instrumented: {c('kcfi_instr', bool)}/{len(rows)}; global funcs without hash preamble: {sum(r['fn_global'] - r['fn_preamble'] for r in rows)} of {sum(r['fn_global'] for r in rows)}")
    if strict_rows:
        cnt = collections.Counter(x[2] for x in strict_rows)
        print("strict compare_ko_v2 vs OS4 stock (non-stock files): " + ", ".join(f"{k}={cnt[k]}" for k in ("STRICT", "metadata-only", "cosmetic", "crc-hack", "REAL")) + f" of {len(strict_rows)} files")
        for kk in ("STRICT", "metadata-only", "cosmetic", "crc-hack", "REAL"):
            names = sorted({x[0] for x in strict_rows if x[2] == kk})
            if names:
                print(f"  strict {kk}: {len(names)} modules: " + ", ".join(names))
    elif strict_note:
        print(strict_note)
    print(f"load lists: vendor_boot {len(vb)}, vendor_dlkm {len(vd)}, system_dlkm {len(sd_)}; listed but not in set {len(notinset)}; dependency cycles {len(cyc)}; "
          f"dependency not available at that stage {len(unavail)}; derived deps not covered by stock vendor_boot modules.dep {nodep}")
    print(f"tracepoint/hook probes: {hook_total} registrations checked, {len(hook_bad)} with no function of the expected kCFI type in the module (modules: {len({x[0] for x in hook_bad})})")
    for h, got, want, ok in hook_res:
        print(f"hook {h}: probe preamble hash {got if got is None else hex(got)} vs vmlinux expects {[hex(x) for x in want]} -> {'OK' if ok else 'MISMATCH (kCFI trap)'}")
    if not a.quiet:
        for r in rows:
            probs = [k for k in ("crc_bad", "crc_missing", "protected_export_unsigned", "dep_missing", "provider_not_in_depends", "export_collide_kernel") if r[k]]
            if probs or not r["vermagic_flags_ok"] or r["depends_vs_stock"].startswith("DIFF") or r["preamble_vs_stock"] not in ("", 0):
                print("  PROBLEM", r["module"], r["part"], probs, r["examples"], r["depends_vs_stock"], "preamble-vs-stock:", r["preamble_vs_stock"])
        for n, part_, cl, f in strict_rows:
            if cl == "REAL":
                print("  STRICT-REAL", n, part_, f, "-> replace by the stock file")
        for n, x, e in hook_bad[:80]:
            print("  HOOK-TYPE", n, x, "vmlinux expects", e)
        for x in cyc[:10]:
            print("  CYCLE", x)
        for x in unavail[:30]:
            print("  UNAVAILABLE", x)
        for x in notinset[:20]:
            print("  LISTED-NOT-IN-SET", x)
    bad_total = sum(1 for r in rows if r["crc_bad"] or r["crc_missing"] or r["protected_export_unsigned"] or r["dep_missing"]
                    or not r["vermagic_flags_ok"] or r["depends_vs_stock"].startswith("DIFF") or r["preamble_vs_stock"] not in ("", 0))
    bad_total += len(cyc) + len(notinset) + len(hook_bad) + sum(1 for x in strict_rows if x[2] == "REAL")
    ok = bad_total == 0 and all(h[3] for h in hook_res)
    print("RESULT:", "PASS" if ok else f"{bad_total} problems (see PROBLEM/CYCLE lines)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
