import sys,subprocess,re
O="<build-dir>"
d,m,f=sys.argv[1:4]
out=subprocess.run([O,"-d","--no-show-raw-insn","<work-dir>"%(d,m)],capture_output=True,text=True).stdout
p=False;L=[]
for l in out.split("\n"):
    if re.match(r"^[0-9a-f]+ <.*>:$",l): p=(l.split("<")[1].rstrip(">:")==f); continue
    if p and re.match(r"^\s+[0-9a-f]+:",l): L.append(l.strip())
brk={}
for l in L:
    mm=re.match(r"([0-9a-f]+):\s+brk\s+(#\S+)",l)
    if mm: brk[mm.group(1)]=mm.group(2)
for i,l in enumerate(L):
    mm=re.search(r"\s(?:b\.\w+|cbz|cbnz|tbz|tbnz)\s[^<]*0x([0-9a-f]+)",l)
    if mm and mm.group(1) in brk:
        print(brk[mm.group(1)], " | ".join(x.split(":",1)[1].strip().replace("\t"," ") for x in L[max(0,i-2):i+1]))
