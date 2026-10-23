"""mk_swinfo_hdr.py <config_platform_data.gz> <out.h>: the stock gz as .byte asm lines (macro PLATFORM_CONFIG_GZ_BYTES) for swinfo_func.c."""
import sys
d = open(sys.argv[1], 'rb').read()
BS = chr(92)
out = ['/* generated from the stock global swinfo.ko (IKCFG_ST..IKCFG_ED payload, gzipped platform .config text); .incbin cannot be used in Kleaf DDK */',
       '#define PLATFORM_CONFIG_GZ_BYTES ' + BS]
for i in range(0, len(d), 32):
    out.append('"' + BS + 't.byte ' + ','.join(str(x) for x in d[i:i + 32]) + BS + 'n" ' + BS)
out.append('""')
open(sys.argv[2], 'w', newline='\n').write('\n'.join(out) + '\n')
