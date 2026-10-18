import re,sys,collections
# callers.py <objdump -dr raw> <func-regex>: who calls (via bl + R_AARCH64_CALL26 .text+off or symbol) the functions matching the regex
raw=open(sys.argv[1],errors='replace').read().split('\n')
pat=re.compile(sys.argv[2])
funcs=[] # (addr,name)
cur=None; calls=[]
for l in raw:
    m=re.match(r'^([0-9a-f]{16}) <([^>]+)>:',l)
    if m: cur=m.group(2); funcs.append((int(m.group(1),16),cur)); continue
    m=re.match(r'^\s*([0-9a-f]+):\s+R_AARCH64_(CALL26|JUMP26)\s+(\S+)',l)
    if m and cur: calls.append((cur,m.group(3)))
byaddr={a:n for a,n in funcs}
tg=collections.defaultdict(list)
for c,t in calls:
    m=re.match(r'\.text\+0x([0-9a-f]+)',t)
    if m: name=byaddr.get(int(m.group(1),16),'?'+t)+'@'+m.group(1)
    else: name=t
    tg[name].append(c)
for k in sorted(tg):
    if pat.search(k): print(k, '<-', sorted(set(tg[k])), len(tg[k]))
