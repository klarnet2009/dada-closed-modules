#!/bin/bash
# nd.sh <module> <func> [maxlines]: normalized diff (- stock, + ours)
cd "/e/<workdir>/work/re/src"
CTX=60 INC=${INC:-stubs_a17B} bash ../g13a_tools/fgq.sh $1 $2 2>&1 | sed -n '/^=== /,$p' | python3 ../g13a_tools/ndiff.py 3 ${3:-600}
