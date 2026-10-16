"""Minimal Android A/B OTA payload.bin extractor (stdlib only; full OTAs: REPLACE, REPLACE_BZ, REPLACE_XZ, ZERO, DISCARD).
Usage: python3 payload_extract.py <payload.bin> <outdir> [partition ...]     (no partition names: list partitions only)"""
import bz2, lzma, struct, sys
from pathlib import Path


def varint(b, i):
    v = s = 0
    while True:
        c = b[i]; i += 1
        v |= (c & 0x7F) << s; s += 7
        if not c & 0x80:
            return v, i


def fields(b):
    """yield (field_number, wire_type, value) of one protobuf message"""
    i = 0
    while i < len(b):
        key, i = varint(b, i)
        f, w = key >> 3, key & 7
        if w == 0:
            v, i = varint(b, i)
        elif w == 2:
            n, i = varint(b, i); v = b[i:i + n]; i += n
        elif w == 1:
            v = b[i:i + 8]; i += 8
        elif w == 5:
            v = b[i:i + 4]; i += 4
        else:
            raise ValueError(f"wire type {w}")
        yield f, w, v


def parse_partition(b):
    p = dict(name="", size=0, ops=[])
    for f, _w, v in fields(b):
        if f == 1:
            p["name"] = v.decode()
        elif f == 7:  # new_partition_info
            for f2, _w2, v2 in fields(v):
                if f2 == 1:
                    p["size"] = v2
        elif f == 8:  # InstallOperation
            op = dict(type=0, off=0, len=0, dst=[])
            for f2, _w2, v2 in fields(v):
                if f2 == 1: op["type"] = v2
                elif f2 == 2: op["off"] = v2
                elif f2 == 3: op["len"] = v2
                elif f2 == 6:
                    e = dict(start=0, n=0)
                    for f3, _w3, v3 in fields(v2):
                        if f3 == 1: e["start"] = v3
                        elif f3 == 2: e["n"] = v3
                    op["dst"].append(e)
            p["ops"].append(op)
    return p


def main():
    path, out, *want = sys.argv[1:]
    with open(path, "rb") as f:
        assert f.read(4) == b"CrAU", "not a payload"
        ver, msize = struct.unpack(">QQ", f.read(16))
        assert ver == 2, ver
        (sigsize,) = struct.unpack(">I", f.read(4))
        manifest = f.read(msize)
        base = 24 + msize + sigsize
        block = 4096
        parts = []
        for fn, _w, v in fields(manifest):
            if fn == 3: block = v
            elif fn == 13: parts.append(parse_partition(v))
        for p in parts:
            kinds = sorted({o["type"] for o in p["ops"]})
            print(f"{p['name']:<18} {p['size']/1048576:9.1f} MiB  ops={len(p['ops']):6}  types={kinds}")
        if not want:
            return
        Path(out).mkdir(parents=True, exist_ok=True)
        for p in parts:
            if p["name"] not in want:
                continue
            with open(Path(out) / (p["name"] + ".img"), "wb") as o:
                o.truncate(p["size"])
                for op in p["ops"]:
                    t = op["type"]
                    if t in (6, 7):  # ZERO / DISCARD: the file is already sparse zeros
                        continue
                    f.seek(base + op["off"])
                    data = f.read(op["len"])
                    if t == 1: data = bz2.decompress(data)
                    elif t == 8: data = lzma.decompress(data)
                    elif t != 0: raise SystemExit(f"unsupported op type {t} in {p['name']}")
                    pos = 0
                    for e in op["dst"]:
                        n = e["n"] * block
                        o.seek(e["start"] * block); o.write(data[pos:pos + n]); pos += n
            print("written", p["name"] + ".img")


main()
