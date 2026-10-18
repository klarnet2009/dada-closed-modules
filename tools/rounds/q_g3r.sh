#!/usr/bin/env bash
# q.sh <module> [stock] : push work/re/src/<module> to server g3r/src and run the ThinLTO emulation (pb_ccl.sh). env DEFS INCS ; prints compile errors + summary
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1; s=${2:-$1}
tar c --exclude=STATUS.md --exclude=attic $m | ssh $K $U "mkdir -p <work-dir> <work-dir> rm -rf <work-dir> tar x -C <work-dir>"
ssh $K $U "cd <work-dir> && DEFS='${DEFS:--DCONFIG_MIEV=1 -DCONFIG_DEBUG_FS=1}' INCS='$INCS' ./pb_ccl.sh $m $s 2>&1 | tail -${TAILN:-25}; cd .. && cp o/$m.lto.o built/$s.ko"
