"""status.py <module> <first line> <section file>: replace the first line of STATUS.md, insert the section after it"""
import sys
m,first,sec=sys.argv[1:4]
p='../src/%s/STATUS.md'%m
s=open(p,'rb').read().decode('utf-8').replace('\r\n','\n')
lines=s.split('\n')
body='\n'.join(lines[1:])
new=first+'\n'+open(sec,'rb').read().decode('utf-8').replace('\r\n','\n').rstrip('\n')+'\n'+body
open(p,'wb').write(new.replace('\n','\r\n').encode('utf-8'))
