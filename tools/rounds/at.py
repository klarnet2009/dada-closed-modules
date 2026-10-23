#!/usr/bin/env python3
"""G40a: resolve '<struct name> <offset>' pairs to member paths in one or more BTFs.
at.py <a.btf>[,<b.btf>...] 'struct ufs_hba' 0x1234 ['struct x' 0x10 ...]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from btfparse import BTF
from curscan import member_at

bs = [(p, BTF.load(p)) for p in sys.argv[1].split(',')]
args = sys.argv[2:]
for name, off in zip(args[::2], args[1::2]):
    o = int(off, 0)
    print('%-28s 0x%x: ' % (name, o) + ' | '.join('%s=%s' % (os.path.basename(p), member_at(b, b.structs()[name][0], o)) for p, b in bs))
