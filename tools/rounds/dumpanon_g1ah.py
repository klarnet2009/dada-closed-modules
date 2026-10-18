import sys
from pathlib import Path
sys.path.insert(0, '<work-dir>')
import btf_dump as B
image, ko = sys.argv[1:3]
base = B.Btf(B.find_base_btf(Path(image).read_bytes()))
mod = B.Btf(B.mod_btf(ko), base)
r = B.Render(mod)
for i, t in enumerate(mod.types):
    if t["kind"] in ("enum","enum64") and not t["name"]:
        print(r.enum_def(t))
    if t["kind"]=="var" or t["kind"]=="datasec":
        pass
