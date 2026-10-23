import sys
sys.path.insert(0,'<work-dir>')
from verify_pkg import Ko, preambles
a=preambles(Ko(sys.argv[1])); b=preambles(Ko(sys.argv[2]))
for n in a:
    if n in b and a[n]!=b[n]: print('%s stock %#x built %#x'%(n,a[n],b[n]))
for n in a:
    if n not in b: print('only stock',n,hex(a[n]))
