#!/bin/bash
# mkdump.sh <module>: (local) create <work-dir> from the OS4 stock .ko on the server, then copy to work/re/g13b_tools/dump_<module>
m=$1
D="<workdir>/work/re/g13b_tools"
bash "$D/sv.sh" "mkdir -p <work-dir> && cd <work-dir> && B=<build-dir> && \$B/llvm-objdump -d -r --no-show-raw-insn stock/$m.ko > dump/$m/dis.txt && \$B/llvm-readelf -sW stock/$m.ko > dump/$m/syms.txt && python3 - $m <<'PY'
import sys
sys.path.insert(0,'<build-dir>')
from compare_ko import Elf
m=sys.argv[1]
e=Elf('stock/%s.ko'%m)
out=open('dump/%s/rodata.txt'%m,'w')
for i,s in enumerate(e.secs):
    if s['name'] in ('.bss','.data','.rodata'): out.write('## %s size=0x%x type=%d\n'%(s['name'],s['size'],s['type']))
i=[k for k,s in enumerate(e.secs) if s['name']=='.rodata'][0]
d=e.data(i); o=0
while o<len(d):
    j=d.find(b'\0',o)
    if j<0: j=len(d)
    if j>o: out.write('.rodata+0x%x\t%r\n'%(o,d[o:j].decode('latin1')))
    o=j+1
out.close()
PY
cd <work-dir> && START_ID=141904 python3 -I bin/btf_dump.py local stock/$m.ko --all > dump/$m/btf_all.txt 2> dump/$m/btf.err; ls -la dump/$m"
mkdir -p "$D/dump_$m"
bash "$D/sv.sh" "cd <work-dir> && tar c $m" | tar x -C "$D" && mv "$D/$m" "$D/dump_$m.new" && rm -rf "$D/dump_$m" && mv "$D/dump_$m.new" "$D/dump_$m"
ls "$D/dump_$m"
