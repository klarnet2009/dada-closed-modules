#!/usr/bin/env bash
# q.sh <module> : push work/re/src/<module> (+stubs_g1) to the server and run the ThinLTO emulation diff vs OS4 stock (tag g1)
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1; s=${2:-$1}; for f in $m/*.c $m/*.h; do tr -d '' < $f > $f.tmp && mv $f.tmp $f; done
tar c --exclude=STATUS.md "$m" | ssh $K $U "rm -rf <work-dir> tar x -C <work-dir>"
ssh $K $U "cd <work-dir> && DEFS='$DEFS' INCS='$INCS' ./pb_ccl.sh $m $s ${XF:--Wno-unused-variable -Wno-unused-function -Wno-unused-const-variable} 2>&1 | tail -${TAILN:-25}; cd .. && cp o/$m.lto.o built/$s.ko && python3 tools/pb_fd.py $s $FD 2>&1 | head -${HEADN:-60}"
