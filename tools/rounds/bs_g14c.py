"""bs.py off... / t<id>... : look up base-BTF string offsets / type ids of kernel.img"""
import sys
sys.path.insert(0, '<work-dir>')
from btf_dump import Btf, find_base_btf
b = Btf(find_base_btf(open('<work-dir>', 'rb').read()))
print('base start', b.start_id, 'ntypes', len(b.types), 'strlen', b.strs_len)
for a in sys.argv[1:]:
    if a[0] == 't':
        t = b.get(int(a[1:])); print(a, t)
    else:
        print(a, b.s(int(a)))
