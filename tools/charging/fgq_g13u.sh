#!/bin/bash
# fgq.sh <module> [func...]: private-dir variant (g13u): uploads src/<module> to <build-dir> BASE=<module>
cd "/e/<workdir>/work/re/src"
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
m=$1; shift
(tar c --exclude=STATUS.md --exclude='*.bak' --exclude='*.py' --transform="s#^$m#${m}_u#" "$m") | ssh $K $U "rm -rf <build-dir> <build-dir> mkdir -p <build-dir> tar x -C <build-dir>"
ssh $K $U "BASE=$m DEFS=\"$DEFS\" <work-dir> ${m}_u ${INC:-stubs_mcaB} ${CTX:-2} ${NODIFF:--} $*" 2>&1
