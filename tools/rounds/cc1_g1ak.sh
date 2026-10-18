#!/usr/bin/env bash
# cc1.sh <module> : compile every .c of the (already uploaded by q.sh) module dir and print ONLY compiler errors/warnings
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
ssh $K $U "cd <work-dir> && for f in *.c; do MODNAME=$1 <work-dir> \$PWD/\$f /tmp/cc1.o -flto=thin -Wno-unused-variable -Wno-unused-function 2>&1 | grep -E 'error|warning' | head -${N:-8}; done"
