import sys
sys.path.insert(0,'<work-dir>')
from verify_pkg import Ko
import re
def syms(p):
    k=Ko(p); d={}
    for name,typ,bind,shndx,val,sz in k.symtab():
        if typ==2 and k.snames[shndx].startswith(('.text','.init.text','.exit.text')):
            d[re.sub(r'\.llvm\.\d+$','',name)]=(bind,val)
    return d
s=syms(sys.argv[1]); b=syms(sys.argv[2])
g=[n for n in s if n in b and s[n][0]!=b[n][0]]
print('binding differ:')
for n in sorted(g): print('  %s stock %s built %s'%(n,'GLOBAL' if s[n][0]==1 else 'local','GLOBAL' if b[n][0]==1 else 'local'))
print('only stock:',[n for n in s if n not in b]); print('only built:',[n for n in b if n not in s])
