# strref.py <module> <func>...: multiset of rodata strings referenced (ADD_ABS_LO12_NC) per function, stock vs built (dir via env D, default built)
import sys,subprocess,re,struct,os,collections
sys.path.insert(0,"<work-dir>")
from crctool import _elf
O="<build-dir>"
m=sys.argv[1]; D=os.environ.get("D","built")
def load(path):
    d=open(path,"rb").read(); named,sec=_elf(d); return d,named
def run(path,funcs):
    d,named=load(path)
    out=subprocess.run([O,"-dr","--no-show-raw-insn",path],capture_output=True,text=True).stdout
    res={}
    cur=None
    for l in out.split("\n"):
        mm=re.match(r"^[0-9a-f]+ <(.*)>:$",l)
        if mm: cur=mm.group(1); continue
        r=re.search(r"R_AARCH64_ADD_ABS_LO12_NC\s+(\S+?)(?:\+0x([0-9a-f]+))?\s*$",l)
        if r and cur in funcs:
            sn,off=r.group(1),int(r.group(2) or "0",16)
            if sn in named and sn.startswith(".rodata"):
                s=named[sn]; o=s[4]+off
                e=d.index(b"\0",o); res.setdefault(cur,collections.Counter())[d[o:e].decode("latin1")]+=1
            elif sn not in named: res.setdefault(cur,collections.Counter())["<sym "+sn+">"]+=1
    return res
fs=set(sys.argv[2:])
a=run("<work-dir>"%m,fs); b=run("<work-dir>"%(D,m),fs)
for f in sys.argv[2:]:
    ca,cb=a.get(f,collections.Counter()),b.get(f,collections.Counter())
    so=ca-cb; bo=cb-ca
    print("%-30s stock-refs %3d built-refs %3d  stock-only %s  built-only %s"%(f,sum(ca.values()),sum(cb.values()),dict(so),dict(bo)))
