"""ro.py <ko> <hexoff>... : print the C string at .rodata+off"""
import sys, struct
d = open(sys.argv[1], 'rb').read()
shoff, = struct.unpack_from('<Q', d, 0x28); shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3a)
secs = []
for i in range(shnum):
    nm, ty, fl, ad, of, sz = struct.unpack_from('<IIQQQQ', d, shoff + i * shentsize)
    secs.append((nm, of, sz))
stroff = secs[shstrndx][1]
def name(n): return d[stroff + n:d.index(b'\0', stroff + n)].decode()
ro = [s for s in secs if name(s[0]) == '.rodata'][0]
for a in sys.argv[2:]:
    o = int(a, 16)
    print(a, d[ro[1] + o:ro[1] + o + 120].split(b'\0')[0])
