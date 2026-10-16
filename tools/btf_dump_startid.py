"""Dump C declarations from a module's split .BTF, resolved against the base BTF embedded in the stock kernel Image.
Usage: python -I btf_dump.py <base_image> <module.ko> [--all]     (default: functions/vars of the module + structs they reach)"""
import struct, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from crctool import _elf

KINDS = "void int ptr array struct union enum fwd typedef volatile const restrict func func_proto var datasec float decl_tag type_tag enum64".split()


class Btf:
    def __init__(self, data, base=None, local=False):
        magic, _v, _f, hlen, toff, tlen, soff, slen = struct.unpack_from("<HBBIIIII", data, 0)
        assert magic == 0xEB9F, hex(magic)
        self.base = base
        self.local = local
        self.start_id = base.start_id + len(base.types) if base else 1
        self.str_base = base.strs_len if base else 0
        self.strs_len = slen + self.str_base
        self.strs = data[hlen + soff:hlen + soff + slen]
        self.types = []  # list of dicts, id = start_id + index
        o, end = hlen + toff, hlen + toff + tlen
        while o < end:
            name_off, info, size_type = struct.unpack_from("<III", data, o)
            o += 12
            kind, vlen, kflag = (info >> 24) & 0x1F, info & 0xFFFF, info >> 31
            t = dict(kind=KINDS[kind], name=self.s(name_off), vlen=vlen, kflag=kflag, st=size_type, members=[])
            if kind == 1:
                t["extra"] = struct.unpack_from("<I", data, o)[0]; o += 4
            elif kind == 3:
                t["arr"] = struct.unpack_from("<III", data, o); o += 12
            elif kind in (4, 5):
                for _ in range(vlen):
                    t["members"].append(struct.unpack_from("<III", data, o)); o += 12
            elif kind == 6:
                for _ in range(vlen):
                    t["members"].append(struct.unpack_from("<Ii", data, o)); o += 8
            elif kind == 13:
                for _ in range(vlen):
                    t["members"].append(struct.unpack_from("<II", data, o)); o += 8
            elif kind == 14:
                o += 4
            elif kind == 15:
                for _ in range(vlen):
                    t["members"].append(struct.unpack_from("<III", data, o)); o += 12
            elif kind == 17:
                o += 4
            elif kind == 19:
                for _ in range(vlen):
                    t["members"].append(struct.unpack_from("<III", data, o)); o += 12
            t["name_off"] = name_off
            self.types.append(t)
        if local:
            self.infer(slen)

    def infer(self, slen):
        """split BTF built against an unknown base: infer where module strings/types start from the data itself"""
        offs = sorted({t["name_off"] for t in self.types if t["name_off"]})
        top = offs[-1]
        cluster = [o for o in offs if o > top - slen]
        self.str_base = min(cluster) if offs and top > 2000000 else 2521890
        # try start_id candidates so that every FUNC points at a FUNC_PROTO
        funcs = [t["st"] for t in self.types if t["kind"] == "func"]
        protos = [j for j, t in enumerate(self.types) if t["kind"] == "func_proto"]
        cands = None
        for st in funcs:
            c = {st - j for j in protos}
            cands = c if cands is None else cands & c
        import os
        self.start_id = int(os.environ['START_ID']) if os.environ.get('START_ID') else (max(cands) if cands else 139319)  # same vendor base for every module (derived from mca_qcom_smem)
        self.cands = sorted(cands or [])
        for t in self.types:  # names were read before the string base was known
            t["name"] = self.s(t["name_off"])

    def s(self, off):
        if self.local:
            off -= self.str_base
            return self.strs[off:self.strs.index(b"\0", off)].decode(errors="replace") if 0 <= off < len(self.strs) else f"?str{off + self.str_base}"
        if self.base and off < self.str_base:
            return self.base.s(off)
        off -= self.str_base
        return self.strs[off:self.strs.index(b"\0", off)].decode(errors="replace")

    def get(self, tid):
        if tid == 0:
            return dict(kind="void", name="void", vlen=0, members=[])
        if tid < self.start_id:
            return self.base.get(tid) if self.base else dict(kind="int", name=f"T{tid}", vlen=0, members=[], st=0)
        return self.types[tid - self.start_id]


def find_base_btf(image):
    pat = struct.pack("<HBBI", 0xEB9F, 1, 0, 24)
    pos = 0
    while (pos := image.find(pat, pos)) != -1:
        hlen, toff, tlen, soff, slen = struct.unpack_from("<IIIII", image, pos + 4)
        if toff == 0 and soff == tlen and 100_000 < tlen < 10_000_000 and slen < 5_000_000:
            return image[pos:pos + hlen + toff + tlen + soff + slen]
        pos += 1
    raise SystemExit("base BTF not found in image")


def mod_btf(ko):
    d = Path(ko).read_bytes()
    named, _ = _elf(d)
    s = named[".BTF"]
    return d[s[4]:s[4] + s[5]]


class Render:
    def __init__(self, btf):
        self.b = btf
        self.defs = {}

    def base(self, t):
        k = t["kind"]
        if k in ("int", "float", "void", "typedef"):
            return t["name"]
        if k in ("struct", "union", "enum", "enum64", "fwd"):
            kw = "union" if (k == "fwd" and t["kflag"]) else ("fwd" if False else k.replace("fwd", "struct").replace("enum64", "enum"))
            return f"{kw} {t['name']}" if t["name"] else f"{kw} <anon>"
        return "?"

    def decl(self, tid, inner=""):
        t = self.b.get(tid)
        k = t["kind"]
        if k in ("const", "volatile", "restrict"):
            q = k
            target = self.b.get(t["st"])
            if target["kind"] == "ptr":
                return self.decl(t["st"], f" {q} {inner}".rstrip() if inner else f" {q}")
            return f"{q} " + self.decl(t["st"], inner)
        if k == "ptr":
            tgt = self.b.get(t["st"])
            star = "*" + inner
            if tgt["kind"] in ("array", "func_proto"):
                star = f"({star})"
            return self.decl(t["st"], star)
        if k == "array":
            return self.decl(t["arr"][0], f"{inner}[{t['arr'][2]}]")
        if k == "func_proto":
            params = ", ".join((self.decl(pt, pn and " " + pn if False else "") if not pn else self.decl(pt, pn))
                               for pn_off, pt in t["members"] for pn in [self.b.s(pn_off)]) if t["members"] else "void"
            if t["members"] and t["members"][-1][1] == 0 and not self.b.s(t["members"][-1][0]):
                params = ", ".join([self.decl(pt, self.b.s(pn_off)) for pn_off, pt in t["members"][:-1]] + ["..."])
            return self.decl(t["st"], f"{inner}({params})")
        if k in ("func",):
            return self.decl(t["st"], inner)
        b = self.base(t)
        return f"{b} {inner}".rstrip() if inner else b

    def struct_def(self, tid, indent=0):
        t = self.b.get(tid)
        pad = "\t" * indent
        kw = t["kind"]
        lines = [f"{pad}{kw} {t['name']} {{".replace("  ", " ")]
        for noff, mtid, off in t["members"]:
            name = self.b.s(noff)
            mt = self.b.get(mtid)
            bits = ""
            if t["kflag"]:
                bits, off = f" : {off >> 24}", off & 0xFFFFFF
            if mt["kind"] in ("struct", "union") and not mt["name"]:
                inner = self.struct_def(mtid, indent + 1).rstrip(";").rstrip()
                lines.append(f"{inner} {name};\t/* +{off // 8} */")
            else:
                lines.append(f"{pad}\t{self.decl(mtid, name)}{bits};\t/* +{off // 8} */")
        size = t["st"]
        lines.append(f"{pad}}}; /* size {size} */")
        return "\n".join(lines)

    def enum_def(self, t):
        return f"enum {t['name']} {{ " + ", ".join(f"{self.b.s(n)} = {v}" for n, v in t["members"]) + " };"


def named_types(r, tid, seen):
    """collect struct/union/enum/typedef ids reachable from tid"""
    out, stack = [], [tid]
    while stack:
        i = stack.pop()
        if i in seen or i == 0:
            continue
        seen.add(i)
        t = r.b.get(i)
        k = t["kind"]
        if k in ("ptr", "const", "volatile", "restrict", "typedef", "func", "type_tag"):
            stack.append(t["st"])
            if k == "typedef" and t["name"]:
                out.append(i)
        elif k == "array":
            stack.append(t["arr"][0])
        elif k == "func_proto":
            stack.append(t["st"]); stack.extend(pt for _n, pt in t["members"])
        elif k in ("struct", "union"):
            if t["name"]:
                out.append(i)
            stack.extend(m[1] for m in t["members"])
        elif k in ("enum", "enum64") and t["name"]:
            out.append(i)
    return out


if __name__ == "__main__":
    image, ko = sys.argv[1:3]
    if image == "local":
        base, mod = None, Btf(mod_btf(ko), None, local=True)
        print(f"/* local mode: start_id candidates {mod.cands[:3]}, str_base {mod.str_base}; T<n> = unresolved base type */")
    else:
        base = Btf(find_base_btf(Path(image).read_bytes()))
        mod = Btf(mod_btf(ko), base)
    r = Render(mod)
    print(f"/* base types {len(base.types) if base else 'unknown'}, module types {len(mod.types)} */")
    funcs, seen = [], set()
    for i, t in enumerate(mod.types):
        tid = mod.start_id + i
        if t["kind"] == "func":
            print(f"{'static ' if t['vlen'] == 0 else ''}{r.decl(tid, t['name'])};")
            named_types(r, t["st"], seen)
        elif t["kind"] == "var":
            print(f"/* var */ {r.decl(t['st'], t['name'])};")
            named_types(r, t["st"], seen)
    print("\n/* ---- types reachable from the module ---- */")
    done = set()
    for tid in sorted(i for i in seen if r.b.get(i)["kind"] in ("struct", "union", "enum", "enum64", "typedef") and r.b.get(i)["name"]):
        t = r.b.get(tid)
        key = (t["kind"], t["name"])
        if key in done:
            continue
        done.add(key)
        if t["kind"] in ("struct", "union"):
            print(r.struct_def(tid))
        elif t["kind"] in ("enum", "enum64"):
            print(r.enum_def(t))
        else:
            print(f"typedef {r.decl(t['st'], t['name'])};")
