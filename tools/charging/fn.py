"""fn.py <dumpdir> <func> [skip] [max]: compact stock listing of one function (reloc targets/strings resolved, adrp/add pairs folded)"""
import sys,re
d,fn=sys.argv[1:3]; skip=int(sys.argv[3]) if len(sys.argv)>3 else 0; mx=int(sys.argv[4]) if len(sys.argv)>4 else 100000
ro={}
for l in open(d+'/rodata.txt',encoding='utf-8',errors='replace'):
    m=re.match(r"\.rodata\+(0x[0-9a-f]+)\s+(.*)",l)
    if m: ro[int(m.group(1),16)]=m.group(2).strip()
lines=open(d+'/dis.txt',encoding='utf-8',errors='replace').read().split('\n')
st=None
for i,l in enumerate(lines):
    if re.match(r"[0-9a-f]+ <%s>:"%re.escape(fn),l): st=i+1;break
assert st, 'no func'
out=[];cur=None
for l in lines[st:]:
    if re.match(r"[0-9a-f]+ <",l): break
    m=re.match(r"\s+([0-9a-f]+):\s+(?:[0-9a-f ]{11}\s+)?(\S.*)$",l)
    r=re.match(r"\s+[0-9a-f]+:\s+(R_AARCH64_\w+)\s+(\S+)",l)
    if r:
        t=r.group(2); ty=r.group(1)
        mm=re.match(r"\.rodata\+(0x[0-9a-f]+)",t)
        if mm:
            s=ro.get(int(mm.group(1),16))
            t='"'+(s[:90] if s else '?')+'"'
        if 'ADD_ABS_LO12' in ty and out and out[-1][1].startswith('add'): out.pop()
        if cur is not None: out[-1][2]=t if 'ADD_ABS' not in ty else out[-1][2]
        continue
    if m:
        a,t=m.groups(); t=re.sub(r"\s*//.*$","",t); t=re.sub(r"<[^>]*>","",t)
        out.append([a,t.replace('\t',' '),'']); cur=1
base=int(out[0][0],16)
for k,(a,t,x) in enumerate(out):
    if k<skip or k>=skip+mx: continue
    t=re.sub(r"0x([0-9a-f]+)\b",lambda m:m.group(0),t)
    print("%4x: %-42s %s"%(int(a,16)-base,t[:60],x[:100]))
