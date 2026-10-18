"""Set the export CRCs AND the import CRCs (__versions) of a rebuilt module to the stock values.
Only for modules whose code is already byte-identical to the stock (compare_ko: everything OK except CRCs): the difference is genksyms type spelling /
a stub provider's struct view, not code. Documented hack, listed in the package manifest.
Usage: python3 crc_patch2.py <built.ko> <stock.ko> <out.ko>   (needs crctool.py next to it or in <build-dir>)"""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, "<build-dir>")
from crctool import _elf, exported, required

built, stock, out = sys.argv[1:4]
want, have = exported(stock), exported(built)
d = bytearray(Path(built).read_bytes())
named, sec = _elf(bytes(d))
stroff = sec[named[".symtab"][6]][4]
kc = {sec.index(named[k]): named["__kcrctab" + k[len("__ksymtab"):]][4] for k in ("__ksymtab", "__ksymtab_gpl")
      if k in named and "__kcrctab" + k[len("__ksymtab"):] in named}
patched = []
so, ss = named[".symtab"][4], named[".symtab"][5]
for o in range(so, so + ss, 24):
    nm, _i, _o, shndx, val, _s = struct.unpack_from("<IBBHQQ", d, o)
    if shndx in kc:
        name = bytes(d[stroff + nm:d.index(bytes(1), stroff + nm)]).decode()
        if name.startswith("__ksymtab_") and name[10:] in want and want[name[10:]] != have.get(name[10:]):
            struct.pack_into("<I", d, kc[shndx] + val // 12 * 4, want[name[10:]])
            patched.append("export " + name[10:])
swant = dict(required(stock))
if "__versions" in named:
    off, size = named["__versions"][4], named["__versions"][5]
    for o in range(off, off + size, 64):
        n = bytes(d[o + 8:d.index(bytes(1), o + 8)]).decode()
        if n in swant and struct.unpack_from("<Q", d, o)[0] != swant[n]:
            struct.pack_into("<Q", d, o, swant[n])
            patched.append("import " + n)
Path(out).write_bytes(d)
assert exported(out) == want or set(want) - set(have), "export CRCs still differ"
assert dict(required(out)) == swant or set(swant) ^ set(dict(required(out))), "import CRCs still differ"
print("patched:", patched)
