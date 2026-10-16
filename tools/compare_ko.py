"""Equivalence report between a stock module and a reconstructed one (run where python3 and the crc tools live).
Per-function comparison (bytes + resolved relocations), so section layout differences (LTO vs non-LTO link, where the
ldapr alternatives or strings are placed) do not matter.  Usage: python3 compare_ko.py <stock.ko> <rebuilt.ko> [-v]"""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from crctool import exported, required

NUL = bytes(1)
CODE = (".text", ".init.text", ".exit.text")


def cstr(b, off):
    return b[off:b.index(NUL, off)].decode(errors="replace")


class Elf:
    def __init__(self, path):
        d = self.d = Path(path).read_bytes()
        shoff, = struct.unpack_from("<Q", d, 0x28)
        es, n, si = struct.unpack_from("<HHH", d, 0x3A)
        raw = [struct.unpack_from("<IIQQQQII", d, shoff + i * es) for i in range(n)]
        so = raw[si][4]
        self.secs = [dict(name=cstr(d, so + r[0]), type=r[1], off=r[4], size=r[5], link=r[6], info=r[7]) for r in raw]
        sym = next(s for s in self.secs if s["type"] == 2)
        strtab = self.secs[sym["link"]]["off"]
        self.syms = []
        for o in range(sym["off"], sym["off"] + sym["size"], 24):
            nm, info, _oth, shndx, val, size = struct.unpack_from("<IBBHQQ", d, o)
            self.syms.append(dict(name=cstr(d, strtab + nm), type=info & 15, bind=info >> 4, shndx=shndx, val=val, size=size))
        self.relocs = {}  # section index -> list of (offset, type, symidx, addend)
        for s in self.secs:
            if s["type"] == 4:
                lst = self.relocs.setdefault(s["info"], [])
                for o in range(s["off"], s["off"] + s["size"], 24):
                    off, info, add = struct.unpack_from("<QQq", d, o)
                    lst.append((off, info & 0xFFFFFFFF, info >> 32, add))

    def data(self, i):
        s = self.secs[i]
        return self.d[s["off"]:s["off"] + s["size"]]

    def owner(self, shndx, addr):
        """function/object symbol containing (section, address) as name+delta"""
        for s in self.syms:
            if s["shndx"] == shndx and s["type"] in (1, 2) and s["val"] <= addr < s["val"] + max(s["size"], 1):
                return f"{s['name']}+{addr - s['val']}"
        return f"{self.secs[shndx]['name']}+{addr}"

    def target(self, symidx, addend):
        s = self.syms[symidx]
        if s["type"] == 3:  # section symbol
            name = self.secs[s["shndx"]]["name"]
            if name.startswith(".rodata"):
                return "str:" + repr(cstr(self.data(s["shndx"]), addend))
            return "@" + self.owner(s["shndx"], addend)
        return f"{s['name']}+{addend}"

    def functions(self):
        out = {}
        for s in self.syms:
            if s["type"] == 2 and s["size"] and s["shndx"] < len(self.secs) and self.secs[s["shndx"]]["name"].startswith(CODE):
                code = self.data(s["shndx"])[s["val"]:s["val"] + s["size"]]
                rel = sorted((off - s["val"], t, self.target(si, add)) for off, t, si, add in self.relocs.get(s["shndx"], [])
                             if s["val"] <= off < s["val"] + s["size"])
                out[s["name"]] = (code, rel)
        return out

    def objects(self):
        return {s["name"]: s["size"] for s in self.syms if s["type"] == 1 and s["bind"] == 1 and s["shndx"] and s["shndx"] < len(self.secs)}

    def modinfo(self):
        i = next(i for i, s in enumerate(self.secs) if s["name"] == ".modinfo")
        kv = dict(p.split(b"=", 1) for p in self.data(i).split(NUL) if b"=" in p)
        return {k.decode(): v.decode(errors="replace") for k, v in kv.items() if k.decode() in ("license", "author", "description", "alias")}


def check(label, ok, detail=""):
    print(f"{'OK  ' if ok else 'DIFF'} {label}{(' - ' + detail) if detail else ''}")
    return ok


if __name__ == "__main__":
    stock, built = sys.argv[1:3]
    verbose = "-v" in sys.argv
    a, b = Elf(stock), Elf(built)
    fa, fb = a.functions(), b.functions()
    same_set = set(fa) == set(fb)
    res = [check("function set", same_set, f"{len(fa)} functions" if same_set else f"only stock: {sorted(set(fa) - set(fb))}, only rebuilt: {sorted(set(fb) - set(fa))}")]
    bad_code = [n for n in fa if n in fb and fa[n][0] != fb[n][0]]
    bad_rel = [n for n in fa if n in fb and fa[n][0] == fb[n][0] and fa[n][1] != fb[n][1]]
    res.append(check("function bytes", not bad_code, "all identical" if not bad_code else "differ: " + ", ".join(bad_code)))
    res.append(check("function relocations (resolved targets)", not bad_rel, "all identical" if not bad_rel else "differ: " + ", ".join(bad_rel)))
    if verbose:
        for n in bad_rel[:3]:
            print("  stock  ", fa[n][1][:12], "\n  rebuilt", fb[n][1][:12])
    oa, ob = a.objects(), b.objects()
    res.append(check("global objects (name,size)", oa == ob, "" if oa == ob else f"stock {oa} vs rebuilt {ob}"))
    ea, eb = exported(stock), exported(built)
    res.append(check("exported symbols + CRCs", ea == eb, f"{len(ea)} symbols" if ea == eb else f"{sorted(ea.items())} vs {sorted(eb.items())}"))
    va, vb = dict(required(stock)), dict(required(built))
    diff = {k: (hex(va.get(k, 0)), hex(vb.get(k, 0))) for k in set(va) | set(vb) if va.get(k) != vb.get(k)}
    res.append(check("imported symbols + CRCs (__versions)", not diff, str(diff) if diff else f"{len(va)} symbols"))
    ma, mb = a.modinfo(), b.modinfo()
    res.append(check("modinfo (license/author/description/alias)", ma == mb, "" if ma == mb else f"{ma} vs {mb}"))
    print("EQUIVALENT" if all(res) else "NOT EQUIVALENT yet")
