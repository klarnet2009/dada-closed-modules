"""names.py : print all struct/union/enum names of the base BTF of <work-dir> (one per line)"""
import sys
sys.path.insert(0, '<work-dir>')
from btf_dump import Btf, find_base_btf
b = Btf(find_base_btf(open('<work-dir>', 'rb').read()))
seen = set()
for t in b.types:
    if t['kind'] in ('struct', 'union', 'enum') and t['name'] and t['name'] not in seen:
        seen.add(t['name']); print(t['kind'][0], t['name'])
