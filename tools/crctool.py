"""Kernel-module ELF helpers: required symbol CRCs (__versions) and exported CRCs (__crc_* ABS symbols).
CLI: crctool.py exports <ko>...   -> TSV  name<TAB>crc<TAB>module   (exports of every given module)"""
import struct, sys
from pathlib import Path


def _elf(d):
    shoff, = struct.unpack_from("<Q", d, 0x28)
    shentsize, shnum, shstr = struct.unpack_from("<HHH", d, 0x3A)
    sec = [struct.unpack_from("<IIQQQQII", d, shoff + i * shentsize) for i in range(shnum)]
    so = sec[shstr][4]
    return {d[so + s[0]:d.index(b"\0", so + s[0])].decode(): s for s in sec}, sec


def required(path):
    d = Path(path).read_bytes()
    named, _ = _elf(d)
    if "__versions" not in named:
        return []
    _n, _t, _f, _a, off, size, *_ = named["__versions"]
    return [(d[o + 8:d.index(b"\0", o + 8)].decode(), struct.unpack_from("<Q", d, o)[0])
            for o in range(off, off + size, 64)]


def exported(path):
    """CRCs of exported symbols: __ksymtab[_gpl] symbol N (12-byte entries) pairs with the 4-byte entry N of __kcrctab[_gpl]."""
    d = Path(path).read_bytes()
    named, sec = _elf(d)
    if ".symtab" not in named:
        return {}
    kc = {sec.index(named[k]): named["__kcrctab" + k[len("__ksymtab"):]][4] for k in ("__ksymtab", "__ksymtab_gpl")
          if k in named and "__kcrctab" + k[len("__ksymtab"):] in named}
    _n, _t, _f, _a, off, size, link, *_ = named[".symtab"]
    stroff = sec[link][4]
    out = {}
    for o in range(off, off + size, 24):
        nm, _info, _oth, shndx, val, _sz = struct.unpack_from("<IBBHQQ", d, o)
        if shndx in kc:
            name = d[stroff + nm:d.index(b"\0", stroff + nm)].decode()
            if name.startswith("__ksymtab_"):
                out[name[10:]] = struct.unpack_from("<I", d, kc[shndx] + val // 12 * 4)[0]
    return out


if __name__ == "__main__" and sys.argv[1] == "exports":
    for p in sys.argv[2:]:
        for n, c in exported(p).items():
            print(f"{n}\t{c:#x}\t{Path(p).stem}")
