"""pairs.py <lst>: (log string, __LINE__) pairs: the mov w2,#N nearest before each __mca_log_* call, with the last x0 string"""
import sys,re
s=None;l=None;out=[]
for line in open(sys.argv[1],encoding='utf-8',errors='replace'):
    m=re.search(r'adrp x0.*"(.*)"',line)
    if m: s=m.group(1)[:70]
    m=re.search(r'mov w2, #(0x[0-9a-f]+)',line)
    if m: l=int(m.group(1),16)
    if re.search(r'bl\s+__mca_log',line):
        out.append('%s | %s'%(l,s)); s=None;l=None
for x in sorted(out): print(x)
