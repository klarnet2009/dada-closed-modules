#!/usr/bin/env bash
# kob.sh <modname e.g. aw882xx_dlkm> [func...] : multiset diff of the LAST real Kleaf build (g15c/built/<mod>.ko; re-run kb.sh first) vs stock; env MIN V
m=$1; shift
bash "$(dirname "$0")/sv.sh" "cd <work-dir> && MOD=$m MIN='$MIN' V='$V' python3 tools2/opb.py <work-dir> $*"
