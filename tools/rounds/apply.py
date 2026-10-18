"""apply.py <file> <old> <new> [<old2> <new2> ...] : python-escaped replace (once each), keeps file newlines"""
import sys
f=sys.argv[1]
s=open(f,encoding='utf8',newline='').read()
crlf='\r\n' in s
s=s.replace('\r\n','\n')
a=sys.argv[2:]
for i in range(0,len(a),2):
    o=a[i].encode().decode('unicode_escape'); n=a[i+1].encode().decode('unicode_escape')
    if s.count(o)!=1: print('COUNT',s.count(o),repr(o)); sys.exit(1)
    s=s.replace(o,n)
if crlf: s=s.replace('\n','\r\n')
open(f,'w',encoding='utf8',newline='').write(s)
print('ok')
