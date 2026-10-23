#!/usr/bin/env python3
import re,sys
txt=open(sys.argv[1],encoding='latin1').read().split('\n== ')
n=0;tot=0
for h in txt:
    lines=h.split('\n')
    S=[l[4:] for l in lines if l.startswith('  - ')]
    B=[l[4:] for l in lines if l.startswith('  + ')]
    if not S and not B: continue
    tot+=1
    def feat(L):
        f=[]
        for l in L:
            l=re.sub(r'^\s*[0-9a-f]+ ','',l)
            mn=l.split(' ')[0]
            if mn in ('cmp','cmn','tst','ccmp','ccmn','mov','movk','orr','and','eor','sub','add','csel','csinc','cinc','cset','csetm','lsl','lsr','asr','mul','sdiv','udiv'):
                if 'sp' in l or 'x29' in l: continue
                if mn in ('add','sub') and not re.search(r'#',l): continue
                f+=[mn+re.sub(r'\b[wx]\d+\b','R',l[len(mn):]).strip()] if mn in ('cmp','cmn','tst','ccmp','ccmn') else re.findall(r'#-?0x[0-9a-f]+|#-?\d+',l)
            if mn.startswith('b.') or mn in ('tbz','tbnz','cbz','cbnz'): f.append(mn.replace('tbz','Z').replace('tbnz','NZ').replace('cbz','Z').replace('cbnz','NZ'))
            if mn=='bl': f.append(l)
            f+=re.findall(r'"[^"]*"',l)
        return sorted(f)
    if feat(S)!=feat(B):
        n+=1; print('== '+h[:1600])
print('# suspicious hunks %d of %d'%(n,tot))
