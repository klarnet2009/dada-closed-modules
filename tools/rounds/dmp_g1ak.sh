#!/usr/bin/env bash
# dmp.sh <module>: make <work-dir> (+syms) of the stock .ko for fn.sh
D="$(dirname "$0")"; m=$1
bash $D/sv.sh "mkdir -p <work-dir> && cd <work-dir> && O=<build-dir> && \$O/llvm-objdump -dr --no-show-raw-insn <work-dir> > dis.txt 2>&1; \$O/llvm-readelf -sW <work-dir> > syms.txt; wc -l dis.txt"
