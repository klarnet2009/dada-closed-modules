import re, sys
mod, func = sys.argv[1], sys.argv[2]
ro={}
for line in open(mod+'/rodata.txt',encoding='utf8',errors='replace'):
    m=re.match(r"^\.rodata\+0x([0-9a-f]+)\t(.*)$",line.rstrip('\n'))
    if m:
        try: ro[int(m.group(1),16)]=eval(m.group(2))
        except: pass
LSTR = {'.L.str.136': 0x130a, '.L.str.141': 0x1338, '.L.str.142': 0x17a7}
def strat(off):
    for k,v in ro.items():
        if k<=off<k+len(v)+1: return v[off-k:]
    return '?'
txt=open(mod+'/dis.txt',errors='replace').read().split('\n')
inf=False; lines=[]
for l in txt:
    m=re.match(r'^[0-9a-f]{16} <([^>]+)>:$',l)
    if m:
        if inf: break
        inf=(m.group(1)==func); continue
    if inf: lines.append(l)
# compact: instruction lines with attached reloc
ins=[]
for l in lines:
    m=re.match(r'^\t\t[0-9a-f]+:\s+R_AARCH64_(\S+)\t(\S+)$',l)
    if m: ins[-1][2]=m.group(2); continue
    m=re.match(r'^\s+([0-9a-f]+):\s+(\S+)\t?(.*)$',l)
    if m: ins.append([m.group(2),m.group(3).split('//')[0].strip(),None])
def roff(rel):
    m=re.match(r'(\.rodata|\.L\.str\.\d+)\+0x([0-9a-f]+)',rel or '')
    return LSTR.get(m.group(1),0)+int(m.group(2),16) if m else None
x2=None; n=0
for i,(op,args,rel) in enumerate(ins):
    if op=='add' and args.startswith('x2, x2') and roff(rel) is not None: x2=roff(rel)
    if op=='bl':
        n+=1
        print(n, hex(x2) if x2 is not None else None, ins[i][2], repr(strat(x2)) if x2 is not None else '')
