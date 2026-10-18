#!/usr/bin/env bash
# go.sh <module>: upload+ThinLTO-emulation compare, then compiler errors (cc1) and an honest summary
cd "$(dirname "$0")/.." || exit 1
bash g1ah_tools/ident.sh $1 2>&1 | tail -6
echo "--- compile errors:"; bash g1ah_tools/cc1.sh $1 | head -${N:-20}
