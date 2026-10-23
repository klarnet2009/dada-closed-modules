#!/usr/bin/env python3
# rng.py m f lo hi [S|B|SB]  prints lst2 lines with offset in [lo,hi] side by side (S then B)
import sys,re
m,f,lo,hi=sys.argv[1],sys.argv[2],int(sys.argv[3],16),int(sys.argv[4],16)
w=sys.argv[5] if len(sys.argv)>5 else 'SB'
for t in w:
    print('--- '+('STOCK' if t=='S' else 'OURS'))
    for l in open('<work-dir>'%(t,m,f),encoding='latin1'):
        x=re.match(r'^\s*([0-9a-f]+):',l)
        if x and lo<=int(x.group(1),16)<=hi: print(re.sub(r'\s{3,}','   ',l.rstrip())[:150])
