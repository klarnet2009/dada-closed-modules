#!/usr/bin/env bash
# mkdis.sh <mod>: llvm-objdump -dr of the last emulated lto.o -> <work-dir>
bash "$(dirname "$0")/sv.sh" "<build-dir> -dr --no-show-raw-insn <work-dir> > <work-dir> ls -la <work-dir>"
