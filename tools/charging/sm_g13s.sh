#!/bin/bash
# sm.sh <module> [maxdiff-words-to-list=100000]: loop + compact summary (errors only if the compile failed)
cd "/e/<workdir>/work/re/src"
out=$(INC=${INC:-stubs_g2i} NODIFF=x bash ../g13s_tools/fgq.sh $1 2>&1)
if echo "$out" | grep -q 'error'; then echo "$out" | grep 'error' | cut -c1-220 | head -${ERRMAX:-12}; exit 1; fi
echo "MISSING: $(echo "$out" | grep MISSING | awk '{print $2}' | tr '\n' ' ')"
echo "$out" | grep -v MISSING | sed 's/ *worddiff=/ /; s/reldiff=//; s/ lines//' | awk -v m=${2:-100000} '$2+0<=m' | sort -k2 -n | tr -s ' ' | tr '\n' ';' | cut -c1-3000; echo
