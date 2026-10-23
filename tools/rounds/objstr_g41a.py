"""objstr.py file.ko symbol [stride]: print NUL-terminated strings at the start of every `stride`-byte record of a data object (of_device_id: stride 200 -> compatible), plus the data-pointer relocation targets."""
import struct, sys
d = open(sys.argv[1], 'rb').read(); sym = sys.argv[2]; stride = int(sys.argv[3]) if len(sys.argv) > 3 else 0
shoff = struct.unpack_from('<Q', d, 0x28)[0]; shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3A)
S = []
for i in range(shnum):
    n, t, f, a, o, sz, l, inf, al, es = struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * shentsize); S.append((n, t, o, sz, l, inf, es))
def nm(off, strsec):
    so = S[strsec][2]; e = d.index(b'\0', so + off); return d[so + off:e].decode()
secn = {i: nm(S[i][0], shstrndx) for i in range(shnum)}
st = [i for i in range(shnum) if S[i][1] == 2][0]
n_ = S[st][3] // 24
for k in range(n_):
    name, info, other, shndx, val, size = struct.unpack_from('<IBBHQQ', d, S[st][2] + k * 24)
    if nm(name, S[st][4]) == sym:
        so = S[shndx][2] + val
        # relocs
        rel = {}
        for i in range(shnum):
            if S[i][1] == 4 and S[i][5] == shndx:
                for r in range(S[i][3] // 24):
                    ro, ri, ra = struct.unpack_from('<QQq', d, S[i][2] + r * 24)
                    if val <= ro < val + size:
                        si = ri >> 32; sn, _, _, sh, sv, _ = struct.unpack_from('<IBBHQQ', d, S[st][2] + si * 24)
                        rel[ro - val] = (nm(sn, S[st][4]) or secn.get(sh, '?')) + '%+d' % ra
        print(sym, secn[shndx], 'size', size)
        if stride == -1:
            # pointer array: print string behind every relocation (target section content at addend, or by symbol)
            for o in sorted(rel):
                tn, ad = rel[o].rsplit('+', 1) if '+' in rel[o] else rel[o].rsplit('-', 1)
                ad = int(rel[o][len(tn):])
                tsec = [i for i in range(shnum) if secn[i] == tn]
                if tsec:
                    b = d[S[tsec[0]][2] + ad:S[tsec[0]][2] + ad + 60]; print('  +%d ->' % o, tn, ad, repr(b.split(b'\x00')[0]))
                else:
                    print('  +%d ->' % o, rel[o])
            break
        if stride:
            for r in range(size // stride):
                b = d[so + r * stride + 64: so + r * stride + 192]; s = b.split(b'\0')[0].decode(errors='replace')
                print(' ', r, repr(s), [v for o, v in sorted(rel.items()) if r * stride <= o < (r + 1) * stride])
        else:
            print(d[so:so + size])
