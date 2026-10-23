#!/usr/bin/env python3
"""dfun.py <raw.dis> : prints function-relative listing: "off: insn ; reloc" ; branch targets converted to +off. Stops at first instruction after symbol end (stops at first reloc-only block)."""
import re, sys
lines = open(sys.argv[1]).read().splitlines()
base = None
ins = []   # (addr, text)
rel = {}
for l in lines:
    m = re.match(r"^([0-9a-f]{16}) <", l)
    if m and base is None:
        base = int(m.group(1), 16); continue
    m = re.match(r"^\s+([0-9a-f]+):\s+(R_AARCH64_\S+)\s+(\S+)", l)
    if m:
        rel.setdefault(int(m.group(1), 16), []).append((m.group(2).replace("R_AARCH64_", ""), m.group(3)))
        continue
    m = re.match(r"^\s+([0-9a-f]+):\s+(.*)$", l)
    if m and base is not None:
        ins.append((int(m.group(1), 16), m.group(2).strip()))
for a, t in ins:
    off = a - base
    t = re.sub(r"\b0x([0-9a-f]+) <[^>]*>", lambda mm: "L%x" % (int(mm.group(1), 16) - base), t)
    t = re.sub(r"\s+", " ", t)
    r = rel.get(a, [])
    print("%05x %s%s" % (off, t, ("   ; " + " ".join("%s:%s" % x for x in r)) if r else ""))
