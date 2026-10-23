import sys,os,glob,subprocess
sys.path.insert(0,'<work-dir>')
from verify_pkg import Ko, preambles
mods=sys.argv[1:]
for m in mods:
    st=glob.glob('<work-dir>'%m,recursive=True)+glob.glob('<work-dir>'%m)
    if not st: print(m,'NOSTOCK'); continue
    bl=sorted(glob.glob('<work-dir>'%m),key=os.path.getmtime)
    if not bl: print(m,'NOBUILT'); continue
    try:
        a=preambles(Ko(st[0])); b=preambles(Ko(bl[-1]))
    except Exception as e:
        print(m,'ERR',e); continue
    com=[n for n in a if n in b]; bad=[n for n in com if a[n]!=b[n]]
    print(m,bl[-1].split('/')[3],len(com),'bad',len(bad),' '.join('%s(%#x/%#x)'%(n,a[n],b[n]) for n in bad))
