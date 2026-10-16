"""Extract the vendor ramdisk fragments of a vendor_boot v4 image and collect every .ko (lz4-legacy cpio, via lz4 + cpio CLIs).
Usage: python3 vendor_boot_extract.py <vendor_boot.img> <outdir>"""
import shutil, struct, subprocess, sys
from pathlib import Path

img, out = Path(sys.argv[1]), Path(sys.argv[2])
b = img.read_bytes()
magic, ver, page, _ka, _ra, rsize = struct.unpack_from("<8sIIIII", b, 0)
assert magic == b"VNDRBOOT" and ver == 4, (magic, ver)
hsize, dtbsz = struct.unpack_from("<II", b, 2096)
tsize, tnum, tesize, bcsize = struct.unpack_from("<IIII", b, 2112)
up = lambda x: (x + page - 1) // page * page
rd_off = up(hsize)
table_off = rd_off + up(rsize) + up(dtbsz)
print(f"header v{ver}, page {page}, ramdisk {rsize} B, table {tnum} entries x {tesize} B")
out.mkdir(parents=True, exist_ok=True)
kos = out / "modules"
kos.mkdir(exist_ok=True)
for i in range(tnum):
    size, off, typ = struct.unpack_from("<III", b, table_off + i * tesize)
    name = b[table_off + i * tesize + 12:table_off + i * tesize + 44].split(bytes(1))[0].decode()
    frag = b[rd_off + off:rd_off + off + size]
    d = out / f"frag{i}_{name or typ}"
    d.mkdir(exist_ok=True)
    (d / "ramdisk.lz4").write_bytes(frag)
    cpio = subprocess.run(["lz4", "-d", "-c", str(d / "ramdisk.lz4")], capture_output=True)
    if cpio.returncode != 0:
        cpio = subprocess.run(["cat", str(d / "ramdisk.lz4")], capture_output=True)  # maybe uncompressed
    (d / "ramdisk.cpio").write_bytes(cpio.stdout)
    subprocess.run(["cpio", "-idm", "--quiet", "-D", str(d)], input=cpio.stdout)
    n = 0
    for p in d.rglob("*.ko"):
        shutil.copy2(p, kos / p.name); n += 1
    print(f"fragment {i}: type {typ} name '{name}' size {size} B -> {n} modules")
print("unique modules:", len(list(kos.glob('*.ko'))))
