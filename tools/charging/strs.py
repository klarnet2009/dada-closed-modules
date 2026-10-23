import sys,subprocess,re
sys.argv=['x','st/cn_mca_strategy_quickchg.ko','st/gl_mca_strategy_quickchg.ko']
src=open('bin/gdiff.py').read().split("cn,gl=sys.argv[1]")[0]
exec(src)
cn,gl=sys.argv[1],sys.argv[2]
rc,rg=strs(cn),strs(gl)
out=subprocess.run([OBJ,"-t",gl],capture_output=True,text=True).stdout
fns=[l.split()[-1] for l in out.splitlines() if ' F ' in l]
def sset(ko,ro):
    d={}
    for fn in fns:
        for t in listing(ko,fn,ro):
            m=re.search(r' @"(.*)"$',t)
            if m: d.setdefault(m.group(1),set()).add(fn)
    return d
a=sset(cn,rc); b=sset(gl,rg)
for k in b:
    if k not in a and 'unable' not in k:
        print(repr(k[:70]), sorted(b[k]))
for k in a:
    if k not in b: print("ONLY-CN",repr(k[:70]),sorted(a[k]))
