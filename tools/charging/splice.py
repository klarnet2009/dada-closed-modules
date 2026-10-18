"""splice.py <file.c> <start_regex> <end_regex> <newtext_file>: replace text from the line matching start_regex up to (not including) the line matching end_regex (searched after start) by newtext_file. LF files."""
import re,sys
p,a,b,n=sys.argv[1:5]
s=open(p,newline='').read()
new=open(n,newline='').read().replace('\r\n','\n')
ma=re.search(a,s,re.M)
if not ma: sys.exit('start not found')
mb=re.compile(b,re.M).search(s,ma.end())
if not mb: sys.exit('end not found')
s=s[:ma.start()]+new.rstrip('\n')+'\n\n'+s[mb.start():]
open(p,'w',newline='').write(s)
print('spliced',ma.start(),mb.start())
