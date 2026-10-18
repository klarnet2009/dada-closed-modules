import sys
p=sys.argv[1]
s=open(p,encoding='utf8').read()
out=[];lines=s.split('\n');i=0
while i<len(lines):
    l=lines[i]
    if l.count('"')%2==1 and i+1<len(lines) and lines[i+1].lstrip().startswith(('"',',')):
        out.append(l+chr(92)+'n'+lines[i+1]); i+=2; continue
    out.append(l); i+=1
open(p,'w',encoding='utf8').write('\n'.join(out))
