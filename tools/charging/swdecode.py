# swdecode.py <func> <tblrodata_off> <adr_base_hex> <maxid> : decode a jump-table switch of the stock function into per-id case summaries
import struct,re,sys,bisect
d=open('tmp/stock_bq.ko','rb').read()
shoff=struct.unpack_from('<Q',d,0x28)[0]; shnum,shstrndx=struct.unpack_from('<HH',d,0x3c)
secs=[]
for i in range(shnum):
    nm,ty,fl,ad,off,sz,lk,inf,al,es=struct.unpack_from('<IIQQQQIIQQ',d,shoff+i*64); secs.append((nm,off,sz))
strt=secs[shstrndx][1]
def nm(o): return d[strt+o:d.index(b'\0',strt+o)].decode()
S={nm(n):(o,s) for n,o,s in secs}
ro=S['.rodata'][0]
def rostr(o):
    e=d.index(b'\0',ro+o); return d[ro+o:e].decode('latin1')
syms=[]
for l in open('../g13s_tools/dump_bq27z561/syms.txt'):
    p=l.split()
    if len(p)>=8 and p[3]=='FUNC': syms.append((int(p[1],16),int(p[2]),p[7]))
syms.sort()
def sym(a):
    i=bisect.bisect_right([s[0] for s in syms],a)-1
    if i>=0 and syms[i][0]<=a<syms[i][0]+syms[i][1]: return syms[i][2]+('' if a==syms[i][0] else '+0x%x'%(a-syms[i][0]))
    return hex(a)
func,toff,adr,maxid=sys.argv[1],int(sys.argv[2],16),int(sys.argv[3],16),int(sys.argv[4],0)
lines=open('../g13s_tools/dump_bq27z561/dis.txt').read().split('\n')
# extract function
body=[];f=False
for l in lines:
    if re.match(r'^[0-9a-f]+ <%s>:'%re.escape(func),l): f=True
    elif f and l=='' : break
    if f: body.append(l)
ins=[] ; 
i=0
while i<len(body):
    m=re.match(r'^\s+([0-9a-f]+):\s+(.*)$',body[i])
    if m:
        a=int(m.group(1),16); t=m.group(2); t=re.sub(r'\s*//.*$','',t)
        rel=''
        if i+1<len(body) and 'R_AARCH64' in body[i+1]:
            r=body[i+1].split(); rel=r[-2]+' '+r[-1]; i+=1
        ins.append((a,t,rel))
    i+=1
tgt={}
for k in range(maxid+1):
    v=struct.unpack_from('<H',d,ro+toff+2*k)[0]
    tgt[k]=adr+(v<<2)
bytgt={}
for k,v in tgt.items(): bytgt.setdefault(v,[]).append(k)
starts=sorted(bytgt)
addrs=[x[0] for x in ins]
def summarize(a0,a1):
    out=[];
    for a,t,rel in ins:
        if not (a0<=a<a1): continue
        if rel.startswith('R_AARCH64_CALL26'):
            out.append('call '+ (sym(int(rel.split('+0x')[-1],16)) if '.text+0x' in rel else rel.split()[-1]))
        elif 'ADR_PREL_PG_HI21' in rel and '.rodata' in rel: 
            o=int(rel.split('+0x')[-1],16); pend=o
            out.append('str@0x%x'%o)
        elif t.startswith('b ') or t.startswith('b.') or t.startswith('cb') or t.startswith('tb'): out.append(t)
        elif t.startswith('mov') and ('#' in t): out.append(t)
        elif rel and 'ADD_ABS' not in rel and 'PREL_PG' not in rel: out.append(t+' ['+rel+']')
    return out
res={}
for idx,s in enumerate(starts):
    e=starts[idx+1] if idx+1<len(starts) else ins[-1][0]+4
    res[s]=(e,summarize(s,e))
for k in range(maxid+1):
    s=tgt[k]
    e,sm=res[s]
    # resolve strings
    out=[]
    for x in sm:
        m=re.match(r'str@0x([0-9a-f]+)',x)
        out.append('"'+rostr(int(m.group(1),16)).replace('\n','\n')[:90]+'"' if m else x)
    print('%3d @%x-%x: %s'%(k+int(sys.argv[5],0) if len(sys.argv)>5 else k,s,e,' | '.join(out)))
