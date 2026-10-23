import re,sys
txt=open(sys.argv[1],encoding='latin1').read().split('\n== ')
n=0;tot=0;out=[]
for h in txt:
    L=h.split('\n')
    S=[l[4:] for l in L if l.startswith('  - ')]; B=[l[4:] for l in L if l.startswith('  + ')]
    if not S and not B: continue
    tot+=1
    def f(X):
        r=[]
        for l in X:
            l=re.sub(r'^\s*[0-9a-f]+ ','',l); mn=l.split(' ')[0]
            if mn.startswith('b.'): r.append(mn)
            if mn in ('csel','csinc','csinv','cinc','cset','csetm'): r.append(mn+':'+l.rstrip().split(' ')[-1])
            if mn in ('ccmp','ccmn'): r.append(mn+':'+l.rstrip().split(' ')[-1])
        return sorted(r)
    if f(S)!=f(B): n+=1; out.append('== '+h[:700])
print('\n'.join(out)); print('# cond-differing hunks %d of %d'%(n,tot))
