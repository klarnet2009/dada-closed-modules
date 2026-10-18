import sys,os,glob
sys.path.insert(0,'<work-dir>')
from verify_pkg import Ko, preambles
def realset(k):
    sym=k.symtab(); iv=[]
    for name,typ,bind,shndx,val,sz in sym:
        if typ==2: iv.append((shndx,val,val+sz))
    d={}
    for name,typ,bind,shndx,val,sz in sym:
        if typ==2: d[name]=(shndx,val)
    def inside(n):
        s,v=d[n]; return any(a==s and b<=v-4 and v-4<c for a,b,c in iv)
    return inside
for m in sys.argv[1:]:
    st=glob.glob('<work-dir>'%m,recursive=True)
    bl=sorted(glob.glob('<work-dir>'%m),key=os.path.getmtime)
    if not st or not bl: continue
    ks=Ko(st[0]); kb=Ko(bl[-1]); a=preambles(ks); b=preambles(kb)
    ins=realset(ks)
    bad=[n for n in a if n in b and a[n]!=b[n] and not ins(n)]
    if bad: print(m,bl[-1].split('/')[3],'REAL',len(bad),' '.join('%s(%#x/%#x)'%(n,a[n],b[n]) for n in bad))
