import sys,re; sys.path.insert(0,'.')
from ed import _rd,_wr
def fnrep(p,fn,old,new,count=1):
    s=_rd(p); old=old.replace('@BS@',chr(92)); new=new.replace('@BS@',chr(92))
    m=re.search(r'^(?:static )?[\w \*]+?\b'+re.escape(fn)+r'\(.*?^\}\n',s,re.S|re.M)
    assert m, fn
    b=m.group(0); assert b.count(old)==count,(fn,b.count(old),old[:50])
    _wr(p,s[:m.start()]+b.replace(old,new)+s[m.end():])
