"""rdump.py <ko> <section> <hexstart> <len> [w]: dump a range of a section as 8-byte rows (or w-byte), with relocation targets (str / sym / sec+off)."""
import sys, struct
sys.path.insert(0, '<work-dir>'); sys.path.insert(0, '<work-dir>')
import tdump
from compare_ko import cstr

e = tdump.Elf(sys.argv[1])
secname = sys.argv[2]
start = int(sys.argv[3], 16)
n = int(sys.argv[4], 0)
w = int(sys.argv[5]) if len(sys.argv) > 5 else 8
for si, s in enumerate(e.secs):
    if s['name'] == secname:
        d = e.data(si)
        rel = {}
        for off, t, sy, add in e.relocs.get(si, []):
            if start <= off < start + n:
                y = e.syms[sy]
                if y['type'] == 3:
                    sn = e.secs[y['shndx']]['name']
                    if sn.startswith('.rodata'):
                        rel[off] = 'str %r' % cstr(e.data(y['shndx']), add)
                    else:
                        rel[off] = 'sec %s+%#x' % (sn, add)
                else:
                    rel[off] = 'sym %s%s' % (y['name'], ('+%#x' % add) if add else '')
        for o in range(start, start + n, w):
            v = int.from_bytes(d[o:o + w], 'little')
            print('+%#x: %0*x %s' % (o, w * 2, v, rel.get(o, '')))
        break
