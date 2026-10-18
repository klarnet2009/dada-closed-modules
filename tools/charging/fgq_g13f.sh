#!/bin/bash
# fgq.sh <module> [func...]: upload src/<module> (all .c/.h) + multi-TU ThinLTO emulation vs OS4 stock (<work-dir>), per-function summary
# env: INC=stubs dir name (default stubs_mcaB), CTX, NODIFF=x
cd "/e/<workdir>/work/re/src"
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1; shift
(tar c --exclude=STATUS.md --exclude='*.bak' --exclude='*.py' "$m") | ssh $K $U "rm -rf <build-dir> <build-dir> mkdir -p <build-dir> tar x -C <build-dir>"
ssh $K $U "DEFS=\"$DEFS\" <work-dir> $m ${INC:-stubs_mcaB} ${CTX:-2} ${NODIFF:--} $*" 2>&1
