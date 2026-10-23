import sys, struct
sys.path.insert(0, '<build-dir>')
from compare_ko import Elf
ko, off, n, base = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 0), int(sys.argv[4], 16)
e = Elf(ko)
i = [k for k, s in enumerate(e.secs) if s['name'] == '.rodata'][0]
d = e.data(i)
t = {}
for k in range(n):
    v = struct.unpack_from('<H', d, off + 2 * k)[0]
    t.setdefault(base + 4 * v, []).append(k)
for a in sorted(t):
    print('%x: %s' % (a, ' '.join('%d' % x for x in t[a])))
