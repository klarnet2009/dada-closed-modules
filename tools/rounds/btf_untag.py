#!/usr/bin/env python3
"""G40a: rewrite BTF TYPE_TAG (kind 18) as CONST without name so the old prebuilt `stg` can read it.
btf_untag.py <in.btf> <out.btf>  (applied to both sides of a comparison, so it cancels out)"""
import struct, sys
b = bytearray(open(sys.argv[1], 'rb').read())
_, _, _, hl, toff, tlen, soff, slen = struct.unpack_from('<HBBIIIII', b, 0)
EXTRA = {1: 4, 3: 12, 14: 4, 17: 4}
VLEN = {4: 12, 5: 12, 6: 8, 19: 12, 13: 8, 15: 12}
p, end, n = hl + toff, hl + toff + tlen, 0
while p < end:
    no, info, st = struct.unpack_from('<III', b, p)
    k, vl = (info >> 24) & 0x1f, info & 0xffff
    if k == 18:
        struct.pack_into('<II', b, p, 0, (10 << 24))
        n += 1
    p += 12 + VLEN.get(k, 0) * vl + EXTRA.get(k, 0)
assert p == end
open(sys.argv[2], 'wb').write(b)
print('type_tag rewritten:', n)
