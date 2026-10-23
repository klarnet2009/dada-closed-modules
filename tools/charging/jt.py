import sys
sys.path.insert(0,'<work-dir>')
from compare_ko_v2 import Elf
import struct
e=Elf(sys.argv[1]); off=int(sys.argv[2],16); base=int(sys.argv[3],16); n=int(sys.argv[4])
i=[k for k,s in enumerate(e.secs) if s.name=='.rodata'][0]
d=e.sdata(i)
for k in range(n):
    v=struct.unpack_from('<H',d,off+2*k)[0]
    print(k,hex(base+4*v))
