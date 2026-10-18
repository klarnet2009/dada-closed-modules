# ann.py <reg> : read fx listing on stdin, annotate [xN/wN-reg, #off] with strategy_fg member paths  (usage: fx.sh f | python3 ann.py x19)
import re,sys,os
reg=sys.argv[1] if len(sys.argv)>1 else 'x19'
shift=int(sys.argv[2]) if len(sys.argv)>2 else 0
t=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'lay_fg.txt')).read()
blk=[b for b in t.split('\n\n') if re.match(r'\s*0 \| struct strategy_fg\s*\n',b)][0]
rows=[];stack=[]
for l in blk.split('\n')[1:]:
    m=re.match(r'\s*(\d+)(?::\d+-\d+)? \|( +)(.*)',l)
    if not m: continue
    off=int(m.group(1)); d=len(m.group(2)); txt=m.group(3)
    name=txt.split()[-1] if txt.split() else ''
    while stack and stack[-1][0]>=d: stack.pop()
    stack.append((d,name))
    rows.append((off,'.'.join(n for _,n in stack[1:]) if len(stack)>1 else name,txt))
def look(o):
    best=None
    for off,p,txt in rows:
        if off<=o and (best is None or off>=best[0]): best=(off,p,txt)
    if best:
        parts=best[1].split('.')
        for i,x in enumerate(parts):
            if x in('work','timer') and i>0: parts=parts[:i]; break
        best=(best[0],'.'.join(parts),best[2])
    return best
for l in sys.stdin:
    l=l.rstrip('\n')
    m=re.search(r'\[%s, #(0x[0-9a-f]+)\]'%reg,l)
    if m:
        o=int(m.group(1),16)+shift; b=look(o)
        if b: l+='   ; %s%s'%(b[1],'' if b[0]==o else '+%d'%(o-b[0]))
    else:
        m=re.search(r'add\s+x\d+, %s, #(0x[0-9a-f]+)'%reg,l)
        if m:
            o=int(m.group(1),16)+shift; b=look(o)
            if b: l+='   ; &%s%s'%(b[1],'' if b[0]==o else '+%d'%(o-b[0]))
    print(l)
