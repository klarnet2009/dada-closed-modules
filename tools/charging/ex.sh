#!/bin/bash
# ex.sh <func> : pin + fast loop with full context; writes $SP/ex_S.txt and ex_O.txt (stock / ours normalised listing), prints normalized diff count and call-sequence differences
SP="$(cat /tmp/sp.txt)"
cd "/e/<workdir>/work/re/src"
python3 ../g13m_tools/pb_pin3.py ../g13m_tools/dump_mca_strategy_buckchg '[mca_strategy_buckchg]%s:%d ' mca_strategy_buckchg/mca_strategy_buckchg.c >/dev/null 2>&1
CTX=3000 INC=stubs_g13b bash ../g13m_tools/fgq.sh mca_strategy_buckchg "$@" 2>&1 | sed -n '/^=== /,$p' > $SP/ex_diff.txt
if ! grep -q '^@@' $SP/ex_diff.txt; then echo "NO DIFF or error:"; head -5 $SP/ex_diff.txt; fi
python3 - "$SP" <<'EOF'
import sys,re,difflib
SP=sys.argv[1]
L=open(SP+'/ex_diff.txt').read().split('\n')
S=[];O=[]
for l in L[2:]:
    if not l or l.startswith('@@'): continue
    c,t=l[0],l[1:]
    if c in ' -': S.append(t)
    if c in ' +': O.append(t)
open(SP+'/ex_S.txt','w').write('\n'.join(S)); open(SP+'/ex_O.txt','w').write('\n'.join(O))
def norm(l):
    l=re.sub(r'\[sp(, #0x[0-9a-f]+)?\]','[sp,#N]',l)
    l=re.sub(r'(add\s+x\d+, sp, )#0x[0-9a-f]+',r'\1#N',l)
    l=re.sub(r'(sub|add)\tsp, sp, #0x[0-9a-f]+',r'\1 sp,sp,#N',l)
    l=re.sub(r'\[x29, #-?0x[0-9a-f]+\]','[x29,#N]',l)
    l=re.sub(r'(stp|ldp)\t(x\d+), (x\d+), \[sp.*','\1 pair',l)
    l=re.sub(r'(str|ldr)\t(x\d+), \[sp.*','\1 reg',l)
    return l
a=[norm(x) for x in S]; b=[norm(x) for x in O]
d=[x for x in difflib.unified_diff(a,b,lineterm='',n=0) if x[:1] in '+-' and x[:3] not in ('+++','---')]
print('insns stock/ours',len(S),len(O),'normalized diff lines',len(d))
def calls(lines):
    r=[]
    for l in lines:
        m=re.search(r'; (\S+)$',l)
        if l.startswith('bl\t') and m: r.append(m.group(1))
        elif l.startswith('ret'): r.append('RET')
        elif l.startswith('adrp') and "str:'" in l:
            m=re.search(r"str:'(.{0,26})",l)
            if m and 'strategy_buckchg' not in m.group(1) and '\\' not in m.group(1)[:3] and m.group(1)[:1]=='[': r.append('S:'+m.group(1)[26-26:][:26])
    return r
x=calls(S); y=calls(O)
sm=difflib.SequenceMatcher(None,x,y,autojunk=False)
for op,i1,i2,j1,j2 in sm.get_opcodes():
    if op!='equal': print(' seq',op,x[i1:i2],'|',y[j1:j2])
EOF
