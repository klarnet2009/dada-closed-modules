import sys,subprocess,difflib,re
sys.argv=['x','st/cn_mca_strategy_quickchg.ko','st/gl_mca_strategy_quickchg.ko']
src=open('bin/gdiff.py').read().split("cn,gl=sys.argv[1]")[0]
exec(src)
cn,gl=sys.argv[1],sys.argv[2]
rc,rg=strs(cn),strs(gl)
out=subprocess.run([OBJ,"-t",gl],capture_output=True,text=True).stdout
fns=[l.split()[-1] for l in out.splitlines() if re.search(r' g? *F .text',l) or ' F ' in l]
import collections
for fn in fns:
    a=listing(cn,fn,rc); b=listing(gl,fn,rg)
    if not a: continue
    d=[l for l in difflib.unified_diff(a,b,lineterm='',n=0) if l[0] in '+-' and not l.startswith(('---','+++'))]
    # drop lines differing only in adrp/add/bl to local .text
    d=[l for l in d if not re.search(r'(bl|b) L$',l)]
    if d: print(fn,len(a),len(b),len(d))
