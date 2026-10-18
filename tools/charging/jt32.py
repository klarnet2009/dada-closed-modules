"""jt32.py <ko> <rodata-off-hex> <n> <base-abs-hex> <func-start-hex>: decode an int32 relative jump table; prints target(rel to func start): [case values]
run on the server (needs <build-dir>)"""
import sys, struct
sys.path.insert(0, '<build-dir>')
from compare_ko import Elf
ko, off, n, base, fs = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 0), int(sys.argv[4], 16), int(sys.argv[5], 16)
e = Elf(ko)
i = [k for k, s in enumerate(e.secs) if s['name'] == '.rodata'][0]
d = e.data(i)
t = {}
for k in range(n):
    v = struct.unpack_from('<i', d, off + 4 * k)[0]
    t.setdefault(base + v - fs, []).append(k)
for a in sorted(t):
    print('%x: %s' % (a, ' '.join('%x' % x for x in t[a])))
