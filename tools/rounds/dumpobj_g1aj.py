import sys,struct
sys.path.insert(0,"<work-dir>")
import tdump
e=tdump.Elf(sys.argv[1])
secname={i:e.secs[i]['name'] for i in range(len(e.secs))}
for s in e.syms:
    if s["type"]!=1 or not s["shndx"] or s["shndx"]>=len(e.secs): continue
    sn=secname[s["shndx"]]
    if sn in ('.bss',) : 
        print("BSS",s["val"],s["size"],s["name"],'LOCAL' if s.get('bind',0)==0 else 'GLOBAL'); continue
    sec=e.data(s["shndx"])
    d=sec[s["val"]:s["val"]+s["size"]]
    r=tdump.reloc_map(e,s)
    if s["name"].startswith("__param_") and not s["name"].startswith("__param_str") and not s["name"].startswith("__param_arr"):
        perm,level,flags=struct.unpack_from("<HbB",d,24)
        print("PARAM",s["name"][8:],"ops=",r.get(16),"perm=",oct(perm),"arg=",r.get(32), "size",s["size"]); continue
    print("DATA",sn,s["val"],s["size"],s["name"],d[:64].hex() if len(d)<=64 else d[:64].hex()+'...',r if r else '')
