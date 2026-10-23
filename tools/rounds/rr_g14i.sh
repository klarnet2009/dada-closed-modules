#!/usr/bin/env bash
# rr.sh <section> <hexstart> <hexend> : relocations of a stock section in a range (e.g. .rodata 4600 4640)
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"
ssh -i "<ssh-key-file>" -o BatchMode=yes <user>@<build-server> "B=<build-dir> \$B/llvm-readelf -r -W <work-dir> | awk -v s=0x$2 -v e=0x$3 'BEGIN{} /^Relocation section/{sec=\$3} \$1 ~ /^[0-9a-f]+\$/ {o=strtonum(\"0x\"\$1); if (sec ~ /$1/ && o>=strtonum(s) && o<strtonum(e)) print sec, \$0}'"
