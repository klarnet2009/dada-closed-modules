"""Set the `depends=` modinfo entry of a rebuilt module to the OS4 stock value (the Kleaf builds name the stub providers there, e.g. depends=minidump_deps_stub,
which would break modprobe/depmod; compare_ko.py does not compare it). The new .modinfo is appended at the end of the file and the section header is
repointed (works when the new string is longer than the old one). Only .modinfo bytes change.
Usage: python3 modinfo_fix.py <built.ko> <stock.ko> <out.ko>"""
import struct, sys
from pathlib import Path


def sections(d):
    shoff, = struct.unpack_from("<Q", d, 0x28)
    es, n, si = struct.unpack_from("<HHH", d, 0x3A)
    so, = struct.unpack_from("<Q", d, shoff + si * es + 24)
    out = {}
    for i in range(n):
        h = shoff + i * es
        nm, = struct.unpack_from("<I", d, h)
        name = d[so + nm:d.index(b"\0", so + nm)].decode()
        out[name] = h
    return out


def modinfo(d):
    h = sections(d)[".modinfo"]
    off, size = struct.unpack_from("<QQ", d, h + 24)
    return h, [e for e in d[off:off + size].split(b"\0") if e]


built, stock, out = sys.argv[1:4]
d = bytearray(Path(built).read_bytes())
sd = Path(stock).read_bytes()
want = [e for e in modinfo(sd)[1] if e.startswith(b"depends=")]
h, ents = modinfo(bytes(d))
if not want:
    print("stock has no depends entry: nothing to do")
    Path(out).write_bytes(d)
    sys.exit(0)
new = [want[0] if e.startswith(b"depends=") else e for e in ents]
if new == ents:
    print("depends already identical")
    Path(out).write_bytes(d)
    sys.exit(0)
blob = b"\0".join(new) + b"\0"
while len(d) % 8:
    d.append(0)
off = len(d)
d += blob
struct.pack_into("<QQ", d, h + 24, off, len(blob))
Path(out).write_bytes(d)
assert [e for e in modinfo(bytes(d))[1] if e.startswith(b"depends=")] == want
print("depends set to", want[0].decode())
