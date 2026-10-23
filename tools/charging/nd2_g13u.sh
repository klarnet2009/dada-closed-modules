#!/bin/bash
# nd.sh <module> <func> [maxlines]: normalized diff (- stock, + ours)
cd "/e/<workdir>/work/re/src"
CTX=60 INC=${INC:-stubs_a17B} bash ../g13u_tools/fgq.sh $1 $2 2>&1 | sed -n '/^=== /,$p' | python3 ../g13u_tools/ndiff2.py 3 ${3:-600}
