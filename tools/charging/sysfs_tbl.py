import struct
d=open('tmp/stock_bq.ko','rb').read()
shoff=struct.unpack_from('<Q',d,0x28)[0]; shnum,shstrndx=struct.unpack_from('<HH',d,0x3c)
secs=[]
for i in range(shnum):
    nm,ty,fl,ad,off,sz,lk,inf,al,es=struct.unpack_from('<IIQQQQIIQQ',d,shoff+i*64); secs.append((nm,off,sz))
strt=secs[shstrndx][1]
def nm(o): return d[strt+o:d.index(b'\0',strt+o)].decode()
S={nm(n):(o,s) for n,o,s in secs}
data=S['.data'][0]; ro=S['.rodata'][0]
rel={}
for l in open('tmp/data_relocs.txt'):
    p=l.split()
    if len(p)>=7 and p[2].startswith('R_'):
        rel[int(p[0],16)]=(p[4],int(p[6],16))
base=0x7090
def rostr(o): return d[ro+o:d.index(b'\0',ro+o)].decode()
def tgt(addr):
    r=rel.get(addr)
    if not r: return None
    return r
out=[]
for i in range(113):
    a=base+i*40
    n=rel[a]; mode=struct.unpack_from('<H',d,data+a+8)[0]
    sh=rel.get(a+16); st=rel.get(a+24); idv=struct.unpack_from('<I',d,data+a+32)[0]
    out.append((i,rostr(n[1]),oct(mode),sh and hex(sh[1]),st and hex(st[1]),idv))
    print(i,rostr(n[1]),oct(mode),sh and (sh[0],hex(sh[1])),st and (st[0],hex(st[1])),idv)
