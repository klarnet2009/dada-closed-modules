#!/usr/bin/env python3
"""dd.py stock.ko built.o stock.dis built.dis outdir func...: write outdir/<func>.s and .b (compact disasm, relocs resolved)"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('rvm', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rv_lib.py'))
rv = importlib.util.module_from_spec(spec); spec.loader.exec_module(rv)
st, bu, sd, bd, out = sys.argv[1:6]
ea, eb = rv.Elf(st), rv.Elf(bu)
fa, fb = ea.functions(), eb.functions()
da, db = rv.parse_dis(sd), rv.parse_dis(bd)
os.makedirs(out, exist_ok=True)
for f in sys.argv[6:]:
    for tag, F, D in (('s', fa, da), ('b', fb, db)):
        code, rel = F[f][0], F[f][1]
        lst = rv.emit(D[f][1], D[f][0], len(code), rel)
        with open(os.path.join(out, f + '.' + tag), 'w') as fh:
            for off, mn, o in lst:
                fh.write('%4x: %-7s %s\n' % (off, mn, o))
