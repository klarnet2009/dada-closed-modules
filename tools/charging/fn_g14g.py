import re,sys
# fn.py <func> : compact listing, relocs merged to the previous insn
lines=open('<workdir>/work/re/g14g_tools/dump_bq27z561/dis.txt',encoding='utf-8',errors='replace').read().split('\n')
for fn in sys.argv[1:]:
    f=False; out=[]
    for l in lines:
        if re.match(r'^[0-9a-f]+ <%s>:'%re.escape(fn),l): f=True; print('== '+fn); continue
        if f and l.strip()=='' : break
        if not f: continue
        m=re.match(r'^\s+([0-9a-f]+):\s+(.*)$',l)
        if m and 'R_AARCH64' not in l:
            out.append([m.group(1),re.sub(r'\s*//.*$','',m.group(2)).replace('\t',' ')])
        elif 'R_AARCH64' in l and out:
            r=l.split(); out[-1][1]+='  ;; '+r[-2].replace('R_AARCH64_','')+' '+r[-1]
    for a,t in out: print('%s: %s'%(a,t))
