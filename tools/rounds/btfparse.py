#!/usr/bin/env python3
"""G40a: minimal BTF parser (raw blob / ELF .BTF / arm64 Image scan), split-BTF aware.

Usage as a library: BTF.load(path[, base]) ; b.structs() ; b.flat(tid) ; b.tstr(tid)
CLI: btfparse.py extract <Image|vmlinux> <out.btf>     (cut the vmlinux BTF blob)
     btfparse.py show <btf> <struct>                     (flat layout of a struct)
"""
import struct, sys

KINDS = {1: 'int', 2: 'ptr', 3: 'array', 4: 'struct', 5: 'union', 6: 'enum', 7: 'fwd', 8: 'typedef',
         9: 'volatile', 10: 'const', 11: 'restrict', 12: 'func', 13: 'func_proto', 14: 'var',
         15: 'datasec', 16: 'float', 17: 'decl_tag', 18: 'type_tag', 19: 'enum64'}
MAGIC = b'\x9f\xeb\x01\x00\x18\x00\x00\x00'


def elf_section(data, want):
    if data[:4] != b'\x7fELF' or data[4] != 2:
        return None
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3a)
    secs = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    stro = secs[shstrndx][4]
    for s in secs:
        n = data[stro + s[0]:data.index(b'\0', stro + s[0])].decode()
        if n == want:
            return data[s[4]:s[4] + s[5]]
    return None


def find_btf(data):
    sec = elf_section(data, '.BTF')
    if sec is not None or data[:4] == b'\x7fELF':
        return sec
    hits = []
    o = data.find(MAGIC)
    while o >= 0:
        _, _, _, hl, toff, tlen, soff, slen = struct.unpack_from('<HBBIIIII', data, o)
        if toff == 0 and soff == tlen and o + hl + soff + slen <= len(data):
            hits.append(data[o:o + hl + soff + slen])
        o = data.find(MAGIC, o + 1)
    assert len(hits) == 1, 'expected exactly one BTF blob, got %d' % len(hits)
    return hits[0]


class BTF:
    def __init__(self, blob, base=None):
        _, _, _, hl, toff, tlen, soff, slen = struct.unpack_from('<HBBIIIII', blob, 0)
        self.base = base
        self.strs = blob[hl + soff:hl + soff + slen]
        self.str_base = len(base.strs) if base else 0
        self.types = list(base.types) if base else [('void', '', 0, 0, 0, None)]
        self.nbase = len(self.types)
        p, end = hl + toff, hl + toff + tlen
        while p < end:
            name_off, info, st = struct.unpack_from('<III', blob, p)
            p += 12
            vlen, kind, kflag = info & 0xffff, (info >> 24) & 0x1f, info >> 31
            k = KINDS[kind]
            extra = None
            if k in ('int', 'var', 'decl_tag'):
                extra = struct.unpack_from('<I', blob, p)[0]; p += 4
            elif k == 'array':
                extra = struct.unpack_from('<III', blob, p); p += 12
            elif k in ('struct', 'union'):
                extra = [struct.unpack_from('<III', blob, p + 12 * i) for i in range(vlen)]; p += 12 * vlen
            elif k == 'enum':
                extra = [struct.unpack_from('<Ii', blob, p + 8 * i) for i in range(vlen)]; p += 8 * vlen
            elif k == 'enum64':
                extra = [struct.unpack_from('<III', blob, p + 12 * i) for i in range(vlen)]; p += 12 * vlen
            elif k == 'func_proto':
                extra = [struct.unpack_from('<II', blob, p + 8 * i) for i in range(vlen)]; p += 8 * vlen
            elif k == 'datasec':
                extra = [struct.unpack_from('<III', blob, p + 12 * i) for i in range(vlen)]; p += 12 * vlen
            self.types.append((k, self.s(name_off), st, kflag, vlen, extra))
        assert p == end, 'BTF type section not consumed exactly (%d != %d)' % (p, end)  # self-check of the record sizes
        self._tstr = {}

    @classmethod
    def load(cls, path, base=None):
        blob = find_btf(open(path, 'rb').read())
        return None if blob is None else cls(blob, base)

    def s(self, off):
        if off >= self.str_base:
            b, off = self.strs, off - self.str_base
        else:
            b = self.base.strs
        return b[off:b.index(b'\0', off)].decode()

    def strip(self, t):  # skip typedef/cv/type_tag
        while self.types[t][0] in ('typedef', 'volatile', 'const', 'restrict', 'type_tag'):
            t = self.types[t][2]
        return t

    def sizeof(self, t):
        t = self.strip(t)
        k, _, st, _, _, ex = self.types[t]
        if k in ('int', 'struct', 'union', 'enum', 'enum64', 'float'):
            return st
        if k == 'ptr':
            return 8
        if k == 'array':
            return ex[2] * self.sizeof(ex[0])
        return 0

    def tstr(self, t, depth=0):
        if t in self._tstr:
            return self._tstr[t]
        k, n, st, _, _, ex = self.types[t]
        if k == 'void':
            r = 'void'
        elif k in ('int', 'float', 'typedef'):
            r = n
        elif k in ('struct', 'union', 'enum', 'enum64', 'fwd'):
            kk = 'enum' if k.startswith('enum') else ('union' if (k == 'fwd' and self.types[t][3]) else ('struct' if k == 'fwd' else k))
            r = '%s %s' % (kk, n) if n else '%s{anon,%d}' % (kk, st)
        elif k == 'ptr':
            r = (self.tstr(st, depth + 1) if depth < 8 else '?') + '*'
        elif k in ('const', 'volatile', 'restrict'):
            r = k + ' ' + self.tstr(st, depth + 1)
        elif k == 'type_tag':
            r = self.tstr(st, depth + 1)
        elif k == 'array':
            r = '%s[%d]' % (self.tstr(ex[0], depth + 1), ex[2])
        elif k == 'func_proto':
            r = 'fn(%s)->%s' % (','.join(self.tstr(a[1], depth + 1) for a in ex), self.tstr(st, depth + 1))
        else:
            r = k + ':' + n
        self._tstr[t] = r
        return r

    def members(self, t):
        k, n, st, kflag, vlen, ex = self.types[t]
        for (no, mt, off) in ex:
            bs, bo = (off >> 24, off & 0xffffff) if kflag else (0, off)
            yield self.s(no), mt, bo, bs

    def flat(self, t, base=0, prefix=''):
        """[(path, bit_off, bit_size, type_str)], anonymous struct/union members inlined."""
        out = []
        for name, mt, bo, bs in self.members(t):
            st = self.strip(mt)
            if not name and self.types[st][0] in ('struct', 'union'):
                out += self.flat(st, base + bo, prefix)
                continue
            size = bs if bs else self.sizeof(mt) * 8
            out.append((prefix + name, base + bo, size, self.tstr(mt)))
            el, sub = st, ''
            while self.types[el][0] == 'array':  # named member of anonymous struct type (or array of it): compare its content too
                el, sub = self.strip(self.types[el][5][0]), sub + '[0]'
            if name and self.types[el][0] in ('struct', 'union') and not self.types[el][1]:
                out += self.flat(el, base + bo, prefix + name + sub + '.')
        return out

    def structs(self, local_only=False):
        """name -> [type ids] of named struct/union definitions."""
        d = {}
        for i in range(self.nbase if local_only else 1, len(self.types)):
            k, n = self.types[i][0], self.types[i][1]
            if k in ('struct', 'union') and n:
                d.setdefault(k + ' ' + n, []).append(i)
        return d

    def embedded(self, t, seen=None):
        """named struct/union types embedded by value (not via pointers) in t, recursively."""
        res = set()
        stack = [t]
        seen = seen if seen is not None else set()
        while stack:
            x = stack.pop()
            for _, mt, _, _ in self.members(x):
                y = self.strip(mt)
                while self.types[y][0] == 'array':
                    y = self.strip(self.types[y][5][0])
                if self.types[y][0] in ('struct', 'union') and y not in seen:
                    seen.add(y)
                    if self.types[y][1]:
                        res.add(y)
                    stack.append(y)
        return res


if __name__ == '__main__':
    if sys.argv[1] == 'extract':
        blob = find_btf(open(sys.argv[2], 'rb').read())
        open(sys.argv[3], 'wb').write(blob)
        print('BTF %d bytes' % len(blob))
    elif sys.argv[1] == 'show':
        b = BTF.load(sys.argv[2])
        for tid in b.structs().get(sys.argv[3], []):
            print('#%d size=%d' % (tid, b.types[tid][2]))
            for p, bo, bs, ts in b.flat(tid):
                print('  0x%04x%s %-40s %s' % (bo // 8, ('.%d:%d' % (bo % 8, bs)) if (bo % 8 or bs % 8) else '', p, ts))
