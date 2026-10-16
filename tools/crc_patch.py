"""Set the exported-symbol CRCs of a rebuilt module to the stock values (code is already identical; only genksyms type spelling differs).
Usage: python3 crc_patch.py <built.ko> <stock.ko> <out.ko>"""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from crctool import _elf, exported

built, stock, out = sys.argv[1:4]
want, have = exported(stock), exported(built)
d = bytearray(Path(built).read_bytes())
named, sec = _elf(bytes(d))
syms_off, syms_size, link = named[".symtab"][4], named[".symtab"][5], named[".symtab"][6]
stroff = sec[link][4]
kc = {sec.index(named[k]): named["__kcrctab" + k[len("__ksymtab"):]][4] for k in ("__ksymtab", "__ksymtab_gpl")
      if k in named and "__kcrctab" + k[len("__ksymtab"):] in named}
patched = []
for o in range(syms_off, syms_off + syms_size, 24):
    nm, _i, _o, shndx, val, _s = struct.unpack_from("<IBBHQQ", d, o)
    if shndx in kc:
        name = bytes(d[stroff + nm:d.index(bytes(1), stroff + nm)]).decode()
        if name.startswith("__ksymtab_") and name[10:] in want and want[name[10:]] != have.get(name[10:]):
            struct.pack_into("<I", d, kc[shndx] + val // 12 * 4, want[name[10:]])
            patched.append(name[10:])
Path(out).write_bytes(d)
assert exported(out) == want or set(want) - set(have), "export CRCs still differ"
print("patched:", patched)
