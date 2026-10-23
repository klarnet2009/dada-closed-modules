"""dumpmod.py <base_image> <module.ko> : print EVERY module-local named struct/union/enum (split BTF resolved against the OS4 kernel Image BTF)."""
import sys
from pathlib import Path
sys.path.insert(0, '<work-dir>')
import btf_dump as B

image, ko = sys.argv[1:3]
base = B.Btf(B.find_base_btf(Path(image).read_bytes()))
mod = B.Btf(B.mod_btf(ko), base)
r = B.Render(mod)
done = set()
for i, t in enumerate(mod.types):
    tid = mod.start_id + i
    if t["kind"] in ("struct", "union") and t["name"]:
        if t["name"] in done:
            continue
        done.add(t["name"])
        print(r.struct_def(tid))
    elif t["kind"] in ("enum", "enum64") and t["name"]:
        print(r.enum_def(t))
    elif t["kind"] == "typedef":
        print(f"typedef {r.decl(t['st'], t['name'])};")
