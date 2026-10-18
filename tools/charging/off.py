import re,sys
t=open(__import__('os').path.join(__import__('os').path.dirname(__file__),'..','g2d_tools','fg','btf.txt')).read()
st=sys.argv[1]
m=re.search(r'^struct %s \{\n(.*?)^\}'%st,t,re.S|re.M)
rows=[]
for l in m.group(1).split('\n'):
    mm=re.match(r'\t(.*?);\t/\* \+(\d+)',l)
    if mm: rows.append((int(mm.group(2)),mm.group(1)))
for a in sys.argv[2:]:
    o=int(a,0); best=None
    for off,d in rows:
        if off<=o: best=(off,d)
    print(a,'->',best, '(+%d)'%(o-best[0]) if best else '')
