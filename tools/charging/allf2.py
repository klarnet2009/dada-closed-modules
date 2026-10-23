import sys,subprocess,difflib,re
m=sys.argv[1]
sys.argv=['x','st/cn_%s.ko'%m,'st/gl_%s.ko'%m]
src=open('bin/gdiff.py').read().split("cn,gl=sys.argv[1]")[0]
exec(src)
cn,gl=sys.argv[1],sys.argv[2]
rc,rg=strs(cn),strs(gl)
out=subprocess.run([OBJ,"-t",gl],capture_output=True,text=True).stdout
fns=[l.split()[-1] for l in out.splitlines() if ' F ' in l]
for fn in fns:
    a=listing(cn,fn,rc); b=listing(gl,fn,rg)
    if not a: 
        if b: print("NEW-IN-GL",fn,len(b))
        continue
    d=[l for l in difflib.unified_diff(a,b,lineterm='',n=0) if l[0] in '+-' and not l.startswith(('---','+++'))]
    d=[l for l in d if not re.search(r'(bl|b) L$',l) and 'mov w2, #LINE' not in l]
    if d: print(fn,len(a),len(b),len(d))
