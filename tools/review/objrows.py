"""objrows.py <ko> <symbol-regex> [width=8]: print the bytes of a named object as rows with relocation targets resolved (compare_ko_v2 Elf)."""
import sys, re
sys.path.insert(0, '<work-dir>')
import compare_ko_v2 as c

e = c.Elf(sys.argv[1])
name = sys.argv[2]
w = int(sys.argv[3]) if len(sys.argv) > 3 else 8
for s in e.syms:
    if s.name == name or re.fullmatch(name, s.name):
        if not s.size or s.shndx >= len(e.secs):
            continue
        sec = e.secs[s.shndx]
        print("== %s %s+%#x size %d" % (s.name, sec.name, s.val, s.size))
        rel = {off: e.tgt(sy, add) for off, t, sy, add in e.rel.get(s.shndx, []) if s.val <= off < s.val + s.size}
        for o in range(0, s.size, w):
            raw = e.d[sec.off + s.val + o: sec.off + s.val + o + w] if sec.type != 8 else b"\0" * w
            v = int.from_bytes(raw, 'little')
            r = rel.get(s.val + o, '')
            print("+%#x: %0*x %s" % (o, w * 2, v, r.split(c.ALT)[0]))
