#!/usr/bin/env bash
# skel.sh <module> <func> : print BUILT function skeleton (branches/calls/cmp) with offsets, and the STOCK one, side by side lists
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
ssh $K $U "cd <work-dir> && <build-dir> -dr --no-show-raw-insn built/$1.ko | awk -v f='<$2>:' '\$2==f{p=1;next} p&&/^[0-9a-f]+ </{exit} p' | grep -vE 'RELOC|R_AARCH64' | sed -E 's/^ *[0-9a-f]+:\s+//' | grep -nE '^(b|cb|tb|bl|ret|cmp|tst|ldar|mov\s+w2[0-9], )' | head -${N:-80}"
