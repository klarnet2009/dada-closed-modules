#!/usr/bin/env bash
# om.sh <func>: multiset instruction diff stock vs ThinLTO emulation (after go.sh); +n = more in built
D="$(dirname "$0")"
scp -q -i "<ssh-key-file>" -o BatchMode=yes $D/opm.py <user>@<build-server>:<work-dir>
bash $D/sv.sh "cd <work-dir> && python3 tools2/opm.py matt $1"
