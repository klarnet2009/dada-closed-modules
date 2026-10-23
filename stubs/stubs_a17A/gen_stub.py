"""gen_stub.py <header.h> <out.c> <header-include-path> [name,name,...]: stub provider (return 0 / empty) for every function prototype in the header"""
import re, sys
hdrs, out, incs = sys.argv[1:4]
hdr_list=hdrs.split(","); inc_list=incs.split(",")
only = set(sys.argv[4].split(",")) if len(sys.argv) > 4 else None
t = chr(10).join(open(h, errors="replace").read() for h in hdr_list)
t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
t = re.sub(r"^\s*#.*$", "", t, flags=re.M)
# drop struct/enum bodies
t = re.sub(r"\b(struct|enum|union)\s+\w*\s*\{[^{}]*(\{[^{}]*\}[^{}]*)*\}\s*;", "", t)
protos = re.findall(r"((?:[A-Za-z_][\w \*]*?)\b(\w+)\s*\(([^;{}]*)\)\s*;)", t)
o = ["// SPDX-License-Identifier: GPL-2.0", "/* generated stub provider: only the export CRCs matter */", "#include <linux/module.h>", ] + [f"#include <{i}>" for i in inc_list] + [""]
seen = set()
for full, name, args in protos:
    if name in seen or (only and name not in only):
        continue
    seen.add(name)
    decl = re.sub(r"\s+", " ", full.strip().rstrip(";"))
    ret = decl[:decl.index(name)].strip()
    if "(*" in decl.split(name)[0] or decl.startswith("typedef"):
        continue
    body = "{ }" if ret == "void" else "{ return 0; }"
    o.append(f"{decl} {body}\nEXPORT_SYMBOL({name});")
o.append('\nMODULE_LICENSE("GPL");')
open(out, "w").write("\n".join(o) + "\n")
print(len(seen), "stubs")
