import re,sys,collections
lines=open('../g13s_tools/dump_bq27z561/dis.txt').read().split('\n')
def body(fn):
    out=[];f=False
    for l in lines:
        if re.match(r'^[0-9a-f]+ <%s>:'%re.escape(fn),l): f=True; continue
        if f and l=='' : break
        if f: out.append(l)
    return out
for fn in sys.argv[1:]:
    b=body(fn); regs={}; calls=collections.Counter(); cmds=collections.Counter()
    i=0
    while i<len(b):
        m=re.match(r'^\s+[0-9a-f]+:\s+(\w+)\s+(.*?)(\s*//.*)?$',b[i])
        if m:
            op,args=m.group(1),m.group(2)
            mm=re.match(r'(w\d+), #(0x[0-9a-f]+|\d+)$',args)
            if op=='mov' and mm: regs[mm.group(1)]=int(mm.group(2),0)
            elif op=='mov' and re.match(r'(w\d+), w\d+$',args): regs.pop(args.split(',')[0],None)
            if op=='bl' and i+1<len(b) and 'R_AARCH64_CALL26' in b[i+1]:
                tg=b[i+1].split()[-1]
                calls[tg]+=1
                if tg in ('.text+0x9cbc','.text+0x1d6c'):
                    cmds[(tg,regs.get('w1'),regs.get('w3'))]+=1
                for r in ('w0','w1','w2','w3','w4'): regs.pop(r,None)
                i+=1
        i+=1
    print('###',fn)
    print(' calls:',', '.join('%s x%d'%(k,v) for k,v in sorted(calls.items(),key=lambda x:-x[1])))
    print(' MAC (w=.text+0x9cbc write, r=.text+0x1d6c read): ',', '.join('%s %s len %s x%d'%('W' if k[0].endswith('9cbc') else 'R',hex(k[1]) if k[1] is not None else '?',k[2],v) for k,v in sorted(cmds.items(),key=lambda x:(x[0][0],x[0][1] or 0))))
