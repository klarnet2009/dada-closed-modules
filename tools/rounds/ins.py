import sys
# ins.py <STATUS.md> <section.md>: insert the section after the first line (CRLF-preserving)
p,sec=sys.argv[1],sys.argv[2]
raw=open(p,'rb').read().decode('utf-8')
crlf='\r\n' in raw
txt=open(sec,'rb').read().decode('utf-8').replace('\r\n','\n')
i=raw.index('\n')
first,rest=raw[:i+1],raw[i+1:]
if crlf: txt=txt.replace('\n','\r\n')
open(p,'wb').write((first+txt+rest).encode('utf-8'))
