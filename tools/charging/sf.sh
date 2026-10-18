#!/bin/bash
# sf.sh <module> [semfp opts/funcs]: semantic fingerprint of the last cclm.sh object vs stock
m=$1; shift
bash "$(dirname $0)/sv.sh" "cd <work-dir> && python3 <work-dir> stock/$m.ko o/$m.lto.o $*"
