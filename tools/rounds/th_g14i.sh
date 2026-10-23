#!/usr/bin/env bash
# th.sh file.c : upload a small C file with function definitions, print the kCFI hash (hex) of each function (kernel flags). Functions must be non-static + non-inline.
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
cd "$(dirname "$0")/.." || exit 1
ssh $K $U "mkdir -p <work-dir>" && scp -q $K "$1" $U:<work-dir>
ssh $K $U "cd <work-dir> && MODNAME=kh <work-dir> \$PWD/t.c <work-dir> -S -O0 2>&1 | grep -E 'error' ; python3 - <<'P'
import re
s=open('<work-dir>').read()
for m in re.finditer(r'\.word\s+(\d+)[^\n]*\n(?:[^\n]*\n){0,3}?(\w+):', s):
    print(m.group(2), hex(int(m.group(1))&0xffffffff))
P"
