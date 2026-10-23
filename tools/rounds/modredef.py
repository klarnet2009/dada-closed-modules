#!/usr/bin/env python3
"""G40a: show how a module-local copy of a kernel struct differs from the base copies.
modredef.py <base.btf> <ko> <struct name> [other.btf]
Prints member-level diff (offset/size/type) between the module copy and the closest base copy
(and the same against other.btf, e.g. ours, when given; the module copy is resolved on base.btf).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from btfparse import BTF, elf_section
from layoutdiff import mdiff

base = BTF.load(sys.argv[1])
mod = BTF(elf_section(open(sys.argv[2], 'rb').read(), '.BTF'), base)
name = sys.argv[3]
for tid in mod.structs(local_only=True).get(name, []):
    fm = mod.flat(tid)
    print('module copy #%d size %d, %d members' % (tid, mod.types[tid][2], len(fm)))
    for label, b in [('base', base)] + ([('other', BTF.load(sys.argv[4]))] if len(sys.argv) > 4 else []):
        best = min(b.structs().get(name, []), key=lambda t: len(mdiff(b.flat(t), fm)))
        md = mdiff(b.flat(best), fm)
        print('  vs %s #%d size %d: %d member diffs' % (label, best, b.types[best][2], len(md)))
        for p, a, c in md[:25]:
            print('    %-30s %s: %s | module: %s' % (p, label, a, c))
