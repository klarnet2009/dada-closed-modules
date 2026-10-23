#!/usr/bin/env python3
"""G40a: infer the vmlinux BTF base a module's split .BTF was built on (no base needed).

btfbase.py <ko>...  -> 'name  N_est  L_est  nlocal  slen'
 N_est: base type count (first local id), from FUNC->FUNC_PROTO consistency
 L_est: base string-section length, from local string-start consistency
Compare with (len(types), len(strs)) of candidate vmlinux BTFs.
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from btfparse import elf_section, KINDS

EXTRA = {'int': 4, 'var': 4, 'decl_tag': 4, 'array': 12}
VLEN = {'struct': 12, 'union': 12, 'enum': 8, 'enum64': 12, 'func_proto': 8, 'datasec': 12}


def raw_types(blob):
    _, _, _, hl, toff, tlen, soff, slen = struct.unpack_from('<HBBIIIII', blob, 0)
    p, end, out = hl + toff, hl + toff + tlen, []
    while p < end:
        no, info, st = struct.unpack_from('<III', blob, p)
        p += 12
        k = KINDS[(info >> 24) & 0x1f]
        vl = info & 0xffff
        mem = []
        if k in VLEN:
            for i in range(vl):
                mem.append(struct.unpack_from('<I', blob, p + VLEN[k] * i)[0])  # member name_off
            p += VLEN[k] * vl
        else:
            p += EXTRA.get(k, 0)
        out.append((k, no, st, mem))
    strs = blob[hl + soff:hl + soff + slen]
    return out, strs


def infer(blob):
    ts, strs = raw_types(blob)
    starts = {0} | {i + 1 for i, c in enumerate(strs[:-1]) if c == 0}
    offs = [t[1] for t in ts if t[1]] + [m for t in ts for m in t[3] if m]
    L = None
    if offs:
        M = max(offs)
        for s in sorted(starts, reverse=True):  # smallest L first
            cand = M - s
            if cand < 0:
                continue
            if all((x - cand) in starts for x in offs if x >= cand):
                L = cand
                break
    funcs = [t[2] for t in ts if t[0] == 'func']
    N = None
    if funcs:
        hi = max(funcs)
        for n in range(max(1, hi - len(ts) + 1), hi + 1):
            if all(f < n or (f - n < len(ts) and ts[f - n][0] == 'func_proto') for f in funcs):
                N = n
                break
    return N, L, len(ts), len(strs)


if __name__ == '__main__':
    for ko in sys.argv[1:]:
        d = open(ko, 'rb').read()
        sec = d if not d.startswith(b'\x7fELF') else elf_section(d, '.BTF')
        if sec is None:
            print(os.path.basename(ko), 'noBTF')
            continue
        print(os.path.basename(ko), *infer(sec))
