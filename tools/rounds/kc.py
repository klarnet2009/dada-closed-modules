"""kc.py <module> func... : print the stock kCFI hash word of each function"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cdis import parse
mod = sys.argv[1]
funcs, order = parse(os.path.join(os.path.dirname(os.path.abspath(__file__)), mod, 'dis.txt'))
for n in sys.argv[2:]:
    h = [t for o, t in funcs[n][:3] if '.word' in t]
    print(n, re.sub(r'.*\.word\s+', '', h[0]) if h else '-')
