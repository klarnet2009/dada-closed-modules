#!/usr/bin/env python3
# fo_scan.py STOCK.ko OURS.ko -> JSON-ish report of field-offset differences per function
import re, subprocess, sys, collections, json
OD='<build-dir>'
def dis(path):
    return subprocess.run([OD,'-d','--no-show-raw-insn',path],capture_output=True,text=True,timeout=600).stdout
RE_F=re.compile(r'^[0-9a-f]{16} <([^>]+)>:$')
RE_I=re.compile(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$')
RE_M=re.compile(r'\[(x\d+|sp)(?:, #(-?0x[0-9a-f]+|-?\d+))?\]')
MEMOPS=re.compile(r'^(ldr|str|ldp|stp|ldur|stur|ldrb|strb|ldrh|strh|ldrsw|ldrsb|ldrsh|ldurb|sturb|ldurh|sturh|ldursw|ldar|stlr|ldapr|ldxr|stxr|ldaxr|stlxr|ldtr|sttr|prfm|ldadd\w*|swp\w*|cas\w*|ldset\w*|ldclr\w*)')
def parse(path):
    funcs={}
    cur=None; sec=None
    for l in dis(path).split('\n'):
        m=RE_F.match(l)
        if m:
            n=re.sub(r'\.llvm\.\d+$','',m.group(1)); n=re.sub(r'^[.]text[.]((unlikely|hot|startup|exit)[.])?','',n); cur=funcs.setdefault(n,[]); curc=set(); ever=set(); continue
        if cur is None: continue
        m=RE_I.match(l)
        if not m: continue
        op,args=m.group(2),m.group(3)
        args=args.split(';')[0].strip()
        regs=re.findall(r'\b([xw]\d+)\b',args.split('[')[0])
        if op=='mrs' and 'SP_EL0' in args:
            curc.add(args.split(',')[0].strip()); ever.add(args.split(',')[0].strip()); continue
        mm=RE_M.search(args)
        if mm and MEMOPS.match(op):
            b=mm.group(1); imm=int(mm.group(2),0) if mm.group(2) else 0
            cur.append((imm,b in curc,op,b in ever))
        # track writes
        if op=='mov' and len(regs)>=1 and re.match(r'^x\d+, x\d+$',args) :
            d,s=args.split(', ')
            if s in curc: curc.add(d)
            else: curc.discard(d)
            continue
        if op in('bl','blr'):
            for i in range(0,19): curc.discard('x%d'%i)
            continue
        if re.match(r'^(str|stp|stur|strb|strh|sturb|sturh|stlr|cmp|cmn|tst|b|b\.\w+|br|cbz|cbnz|tbz|tbnz|ret|retaa|autiasp|paciasp|nop|dmb|dsb|isb|prfm|bti|hint)$',op): continue
        if regs:
            curc.discard(regs[0].replace('w','x'))
            if op.startswith('ldp') and len(regs)>1: curc.discard(regs[1].replace('w','x'))
    return funcs
def summ(acc):
    cs=collections.Counter(i for i,c,o,e in acc if c)
    bg=collections.Counter(i for i,c,o,e in acc if i>=0x100)
    ls=collections.Counter(i for i,c,o,e in acc if e)
    return cs,bg,ls
a=parse(sys.argv[1]); b=parse(sys.argv[2])
out={'funcs':{}}
ta=collections.Counter(); tb=collections.Counter(); la=collections.Counter(); lb=collections.Counter()
for n in sorted(set(a)&set(b)):
    ca,ba,xa=summ(a[n]); cb,bb,xb=summ(b[n])
    ta.update(ca); tb.update(cb); la.update(set(xa)); lb.update(set(xb))
    d={}
    if set(xa)!=set(xb): d['loose_stock_only']=sorted(hex(x) for x in set(xa)-set(xb)); d['loose_ours_only']=sorted(hex(x) for x in set(xb)-set(xa))
    if set(ca)!=set(cb): d['cur_stock_only']=sorted(hex(x) for x in set(ca)-set(cb)); d['cur_ours_only']=sorted(hex(x) for x in set(cb)-set(ca))
    if ba!=bb:
        sa=sorted(hex(k)+('x%d'%(ba[k]-bb[k])) for k in ba if ba[k]>bb[k]); sb=sorted(hex(k)+('x%d'%(bb[k]-ba[k])) for k in bb if bb[k]>ba[k])
        d['big_stock_more']=sa; d['big_ours_more']=sb
    if d: out['funcs'][n]=d
out['only_stock']=sorted(set(a)-set(b)); out['only_ours']=sorted(set(b)-set(a)); out['nfuncs']=len(set(a)&set(b))
out['module_cur_diff']={hex(k):[ta[k],tb[k]] for k in set(ta)|set(tb) if ta[k]!=tb[k]}
out['module_loose_setdiff']={hex(k):[la[k],lb[k]] for k in set(la)|set(lb) if (la[k]>0)!=(lb[k]>0)}
print(json.dumps(out))
