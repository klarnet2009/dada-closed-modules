import re,sys,difflib
sys.argv=[sys.argv[0]]+sys.argv[1:]
exec(open('cmpstock.py').read().split("cn=load(sys.argv[1])")[0])
cn=load('cn_dis.txt'); gl=load('gl_dis.txt')
for n in sys.argv[1:]:
    a,la=norm(cn[n],None); b,lb=norm(gl[n],None)
    def key(x):
        t,r=x; r=re.sub(r'\.(text|rodata|data|bss)\+0x[0-9a-f]+',r'.\1+X',r); return t+' |'+r
    ka=[key(x) for x in a]; kb=[key(x) for x in b]
    print('==',n)
    for tag,i1,i2,j1,j2 in difflib.SequenceMatcher(None,ka,kb,autojunk=False).get_opcodes():
        if tag!='equal':
            print(' CN',i1,ka[i1:i2][:6]); print(' GL',j1,kb[j1:j2][:6])
