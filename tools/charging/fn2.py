"""fn2.py <listing-from-fx.sh> <func-base-hex>: rewrite absolute branch targets to function-relative offsets"""
import sys,re
base=int(sys.argv[2],16)
for l in open(sys.argv[1],encoding='utf-8',errors='replace'):
    l=l.rstrip('\n')
    m=re.match(r'(\s*[0-9a-f]+: )(b\.\w+|b|cbz|cbnz|tbz|tbnz|bl)( .*)$',l)
    if m:
        rest=m.group(3)
        def f(mm):
            v=int(mm.group(1),16)-base
            return '@%x'%v if v>=0 else mm.group(0)
        if m.group(2)=='bl' and re.search(r'0x[0-9a-f]+ +\S',rest):
            pass
        else:
            rest=re.sub(r'0x([0-9a-f]+)',f,rest)
        l=m.group(1)+m.group(2)+rest
    print(l)
