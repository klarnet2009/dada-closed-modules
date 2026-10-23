#!/usr/bin/env bash
# who.sh <mod> <string-fragment> : which stock functions reference the .rodata string containing the fragment
bash "$(dirname "$0")/sv.sh" "cd <work-dir> python3 - '$1' '$2' <<'PY'
import struct,sys,re,subprocess
mod,frag=sys.argv[1],sys.argv[2].encode()
d=open('stock/%s.ko'%mod,'rb').read()
shoff=struct.unpack_from('<Q',d,0x28)[0]; n,ss=struct.unpack_from('<HH',d,0x3c)
so=struct.unpack_from('<IIQQQQ',d,shoff+ss*64)[4]
offs=[]
for i in range(n):
    nm,t,f,a,o,sz=struct.unpack_from('<IIQQQQ',d,shoff+i*64)
    if d[so+nm:d.index(b'\\0',so+nm)]==b'.rodata':
        j=0
        while True:
            j=d.find(frag,o,o+sz)
            if j<0: break
            s=j
            while d[s-1:s]!=b'\\0': s-=1
            offs.append(s-o); o=j+1; sz=0 if False else sz
            break
print([hex(x) for x in offs])
fn=None; res=set()
for l in open('o/stock_%s.dis'%mod):
    m=re.match(r'^[0-9a-f]{16} <([^>]+)>:',l)
    if m: fn=m.group(1); continue
    for off in offs:
        if ('.rodata+0x%x'%off) in l: res.add(fn)
print(sorted(res))
PY"
