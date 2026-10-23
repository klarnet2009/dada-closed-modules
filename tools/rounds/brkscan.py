# brkscan.py <ko-dir-a> <ko-dir-b> module func... : per function: brk imm histogram + sorted string literal refs (relocs to .rodata.str via objdump -s not used: compare via symbolic strings is in compare_ko_v2; here brk histogram + insn count)
import sys,subprocess,re,collections
O="<build-dir>"
def fn(path,f):
    out=subprocess.run([O,"-dr","--no-show-raw-insn",path],capture_output=True,text=True).stdout
    p=False;L=[]
    for l in out.split("\n"):
        if re.match(r"^[0-9a-f]+ <.*>:$",l): p=(l.split("<")[1].rstrip(">:")==f); continue
        if p: L.append(l)
    return L
m=sys.argv[1]
for f in sys.argv[2:]:
    for w,d in (("stock","stock"),("built","built")):
        L=fn("<work-dir>"%(d,m),f)
        ins=[l for l in L if re.match(r"^\s+[0-9a-f]+:\s",l)]
        brk=collections.Counter(re.search(r"brk\s+(#\S+)",l).group(1) for l in ins if "brk" in l)
        # branches to brk blocks
        addr={}
        for l in ins:
            mm=re.match(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*)",l); addr[mm.group(1)]=(mm.group(2),mm.group(3))
        brkaddr={a:re.search(r"#\S+",v[1]).group(0) for a,v in addr.items() if v[0]=="brk"}
        br=collections.Counter()
        for a,(op,args) in addr.items():
            if op.startswith("b") or op in("cbz","cbnz","tbz","tbnz"):
                t=re.search(r"0x([0-9a-f]+)",args)
                if t and t.group(1) in brkaddr: br[brkaddr[t.group(1)]]+=1
        strs=sorted(re.sub(r"^.*R_AARCH64_\S+\s+","",l) for l in L if "R_AARCH64" in l and ("str" in l))
        print("%-28s %-5s insns %4d brk-insns %s branches-to-brk %s"%(f,w,len(ins),dict(brk),dict(br)))
