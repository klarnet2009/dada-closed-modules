"""gdiff.py <cn.ko> <gl.ko> <func...>: normalised diff (- cn, + gl); relocs resolved, addrs/branch targets stripped"""
import re, subprocess, sys, difflib
sys.path.insert(0, "<build-dir>")
OBJ="<build-dir>"
def strs(ko):
    from compare_ko import Elf
    e=Elf(ko); i=[k for k,s in enumerate(e.secs) if s['name']=='.rodata'][0]; return e.data(i)
def listing(ko, fn, ro):
    out=subprocess.run([OBJ,"-dr","--no-show-raw-insn","--disassemble-symbols="+fn,ko],capture_output=True,text=True).stdout.splitlines()
    ins=[]; rel={}
    for l in out:
        m=re.match(r'\s+([0-9a-f]+):\s+(R_AARCH64_\w+)\s+(\S+)',l)
        if m:
            rel[int(m.group(1),16)]=(m.group(2),m.group(3)); continue
        m=re.match(r'\s+([0-9a-f]+):\s+(.*)$',l)
        if m: ins.append((int(m.group(1),16),m.group(2)))
    if not ins: return []
    b0=ins[0][0]; rel={(k+b0 if k<b0 else k):v for k,v in rel.items()}
    base=ins[0][0]; res=[]
    for a,t in ins:
        t=re.sub(r'//.*$','',t).strip()
        t=re.sub(r'\s+0x[0-9a-f]+ <[^>]*>$',' L',t)
        t=re.sub(r'<[^>]*>','',t); t=re.sub(r'\s+',' ',t)
        r=rel.get(a)
        if r:
            ty,sym=r; mm=re.match(r'\.rodata\+0x([0-9a-f]+)',sym)
            if mm:
                o=int(mm.group(1),16); e=ro.find(b'\0',o); sym='"'+ro[o:e].decode('latin1')[:60]+'"'
            elif ty.endswith('ADR_PREL_PG_HI21') or 'ADD_ABS_LO12' in ty: pass
            t+=' @'+sym
        t=re.sub(r' @(\.text|\.bss|\.data|\.rodata)[+0-9a-fx]*$','',t) if True else t
        res.append(t)
    for i,t in enumerate(res):
        if t.startswith('mov w2, #0x') and any('__mca_log' in x for x in res[i+1:i+4]): res[i]='mov w2, #LINE'
    return res
cn,gl=sys.argv[1],sys.argv[2]
rc,rg=strs(cn),strs(gl)
for fn in sys.argv[3:]:
    a=listing(cn,fn,rc); b=listing(gl,fn,rg)
    print("=====",fn,"cn",len(a),"gl",len(b))
    for l in difflib.unified_diff(a,b,lineterm='',n=2): print(l)
