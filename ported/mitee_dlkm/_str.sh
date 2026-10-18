#!/usr/bin/env bash
# _str.sh 0xOFF...: strings of the stock .rodata at the given offsets
ssh -i "<ssh-key-file>" -o BatchMode=yes <user>@<build-server> "python3 -c \"
import sys
d=open('<work-dir>','rb').read()
for a in sys.argv[1:]:
    o=int(a,16); e=d.index(b'\\0',o); print(a, d[o:e])
\" $*"
