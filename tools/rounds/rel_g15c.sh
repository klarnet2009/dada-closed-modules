#!/usr/bin/env bash
# rel.sh <stock|built> <mod> <section> <hexstart> <hexlen> : relocations of an address range (server, g15c)
exec bash "$(dirname "$0")/sv.sh" "cd <work-dir> && python3 tools/rel.py $1 $2 $3 $4 $5"
