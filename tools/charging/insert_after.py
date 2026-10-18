"""insert_after.py <STATUS.md> <anchor text (start of line to insert after the whole line)> <file with text>"""
import sys
p,anchor,f=sys.argv[1:4]
s=open(p,'rb').read().decode().replace('\r\n','\n')
i=s.index(anchor); j=s.index('\n',i)
s=s[:j]+'\n'+open(f,'rb').read().decode().replace('\r\n','\n').rstrip('\n')+s[j:]
open(p,'wb').write(s.replace('\n','\r\n').encode())
