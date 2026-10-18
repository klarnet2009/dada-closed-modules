#!/usr/bin/env bash
# fd.sh <mod> [grep]: per-function masked word diff stock vs the last built .ko (server g3t)
exec bash "$(dirname "$0")/sv.sh" "cd <work-dir> && python3 tools/fdiff.py stock/$1.ko built/$1.ko 2>&1 | grep -E '${2:-.}'"
