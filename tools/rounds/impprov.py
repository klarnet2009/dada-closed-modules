# impprov.py <mods_root> <module.ko>...: for each non-vmlinux import show provider module in the package + CRC match
import sys,glob,os
sys.path.insert(0,"<work-dir>")
from crctool import required,exported
root=sys.argv[1]
ex={}
for f in glob.glob(root+"/*/*.ko"):
    try:
        for n,c in exported(f).items(): ex.setdefault(n,[]).append((os.path.basename(f)[:-3],c))
    except Exception as e: pass
for m in sys.argv[2:]:
    r=required(m); bad=0; n_pkg=0
    prov={}
    for n,c in r:
        if n in ex:
            n_pkg+=1
            ok=any(c==cc for _,cc in ex[n])
            if not ok: bad+=1
            prov.setdefault(ex[n][0][0],[]).append("%s%s"%(n,"" if ok else "!CRC"))
    print("##",os.path.basename(m),"imports",len(r),"from-package-modules",n_pkg,"crc-mismatch",bad)
    for k,v in prov.items(): print("   ",k,":",",".join(v))
