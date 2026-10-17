"""crcpad.py <module-src.c> <sym>=<stock_crc>=<built_crc>... : CRC pad without genksyms access (affine CRC trick).
Prerequisite: the export is defined with a 14-char placeholder pad `GKPAD(fcrcQQQQQQ00NN)` (extra `struct tag *` parameter under #ifdef __GENKSYMS__, see rtg.c);
build once with Bazel (bz.sh), read the BUILT crc of every pad symbol from compare_ko, then run this: it computes for each placeholder a replacement tag
so that crc32 matches the stock value (crc32 of equal-length messages is affine: only the tag and the fixed tail ' { UNKNOWN } * ) ' matter)."""
import sys, zlib, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'a17A_scripts'))
from forcecrc import force
SUF = " { UNKNOWN } * ) "
f = sys.argv[1]
s = open(f).read()
for a in sys.argv[2:]:
    sym, stock, built, ph = a.split('=')
    tp = int(stock, 0) ^ int(built, 0) ^ zlib.crc32((ph + SUF).encode())
    new = force('', SUF, tp)
    assert len(new) == len(ph)
    s = s.replace(ph, new)
    print(sym, ph, '->', new)
open(f, 'w').write(s)
