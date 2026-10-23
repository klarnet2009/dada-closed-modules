# cmpstock.py <cn_dis.txt> <gl_dis.txt> : per-function normalised diff CN stock vs GLOBAL stock; line numbers of __mca_log calls abstracted
import re,sys,json
def load(p):
    funcs={}; cur=None
    for l in open(p,encoding='utf-8',errors='replace'):
        l=l.rstrip('\n')
        m=re.match(r'^([0-9a-f]+) <(.+)>:$',l)
        if m: cur=m.group(2); funcs[cur]=[]; continue
        if cur is None or not l.strip(): continue
        m=re.match(r'^\s+([0-9a-f]+):\s+(.*)$',l)
        if m and 'R_AARCH64' not in l:
            funcs[cur].append([re.sub(r'\s*//.*$','',m.group(2)).replace('\t',' '),''])
        elif 'R_AARCH64' in l and funcs[cur]:
            r=l.split(); funcs[cur][-1][1]=r[-2].replace('R_AARCH64_','')+' '+r[-1]
    return funcs
def norm(f,names_by_text):
    out=[]; lines=[]
    for i,(t,r) in enumerate(f):
        t=re.sub(r'<[^>]*>','',t)
        t=re.sub(r'\b0x[0-9a-f]+ *$','L',t) if re.match(r'(b|bl|b\.\w+|cbn?z|tbn?z)\b',t) else t
        t=re.sub(r'\s+',' ',t).strip()
        t=re.sub(r'^(adrp x\d+), 0x[0-9a-f]+$',r', PG',t)
        out.append([t,r])
    # abstract log line immediates
    for i,(t,r) in enumerate(out):
        if t.startswith('bl') and re.search(r'__mca_log_(err|info|dbg|warn|debug)',r) :
            for j in range(i-1,max(i-20,-1),-1):
                m=re.match(r'mov w2, #(0x[0-9a-f]+|-?\d+)$',out[j][0])
                if m:
                    lines.append(int(m.group(1),0)); out[j][0]='mov w2, #LINE'; break
    return out,lines
cn=load(sys.argv[1]); gl=load(sys.argv[2])
res={}; pairs=[]
for n in sorted(set(cn)&set(gl)):
    a,la=norm(cn[n],None); b,lb=norm(gl[n],None)
    # compare text+reloc symbol names, mask .text+0x addends and anon rodata
    def key(x):
        t,r=x
        r=re.sub(r'\.(text|rodata|data|bss)\+0x[0-9a-f]+',r'.\1+X',r)
        return t+'|'+r
    ka=[key(x) for x in a]; kb=[key(x) for x in b]
    if ka==kb:
        res[n]=('same-after-lines' if la!=lb else 'identical',0)
        if la!=lb: pairs+=list(zip(la,lb))
    else:
        import difflib
        sm=difflib.SequenceMatcher(None,ka,kb,autojunk=False)
        nd=sum(max(i2-i1,j2-j1) for tag,i1,i2,j1,j2 in sm.get_opcodes() if tag!='equal')
        res[n]=('REAL',nd,len(ka),len(kb))
        if len(la)==len(lb): pairs+=list(zip(la,lb))
print('only cn:',sorted(set(cn)-set(gl)))
print('only gl:',sorted(set(gl)-set(cn)))
from collections import Counter
print(Counter(v[0] for v in res.values()))
for n,v in res.items():
    if v[0]=='REAL': print('REAL',n,v[1:])
json.dump(pairs,open('linepairs.json','w'))
print('pairs',len(pairs))
