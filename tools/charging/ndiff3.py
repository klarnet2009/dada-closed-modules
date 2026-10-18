"""ndiff.py: stdin = fgq.sh output (unified diffs); re-diffs each function after masking sp offsets / frame sizes"""
import sys,re,difflib
txt=sys.stdin.read().split('\n')
def norm(l):
    l=re.sub(r'\[sp(, #0x[0-9a-f]+)?\]','[sp,#N]',l)
    l=re.sub(r'(add\s+x\d+, sp, )#0x[0-9a-f]+',r'\1#N',l)
    l=re.sub(r'(sub|add)\tsp, sp, #0x[0-9a-f]+',r'\1 sp,sp,#N',l)
    l=re.sub(r'\[x29, #-?0x[0-9a-f]+\]','[x29,#N]',l)
    l=re.sub(r'(stp|ldp)\t(x\d+), (x\d+), \[sp.*','\1 pair',l)
    l=re.sub(r'(str|ldr)\t(x\d+), \[sp.*','\1 reg',l)
    return l
cur=None;S=[];M=[];name=''
def strip(L):
    o=[]
    for l in L:
        l=re.sub(r'(?<![0-9a-zA-Z#])[wx][0-9]+(?![0-9a-zA-Z])','R',l)
        l=re.sub(r'\[x29,#N\]','[x29,#N]',l)
        l=re.sub(r'ADDR.*','ADDR',l) if 'bl' not in l[:3] and not l.startswith('b') else l
        o.append(l)
    return o

def flush():
    if not name: return
    a=strip([norm(x) for x in S]);b=strip([norm(x) for x in M])
    d=[x for x in difflib.unified_diff(a,b,lineterm='',n=int(sys.argv[1]) if len(sys.argv)>1 else 2)][2:]
    print('=== %s: normalized diff lines: %d'%(name,len([x for x in d if x[0] in '+-'])))
    print('\n'.join(d[:int(sys.argv[2]) if len(sys.argv)>2 else 80]))
for l in txt:
    if l.startswith('=== '):
        flush(); name=l[4:].split(':')[0]; S=[];M=[]; continue
    if l.startswith('@@'): continue
    if not name: continue
    if l.startswith('-'): S.append(l[1:])
    elif l.startswith('+'): M.append(l[1:])
    elif l.startswith(' '): S.append(l[1:]);M.append(l[1:])
flush()
