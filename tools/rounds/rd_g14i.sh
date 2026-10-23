#!/usr/bin/env bash
# rd.sh <section> <hexstart> <len> [w] : dump stock section range with relocs
cd "$(dirname "$0")"
scp -q -i "<ssh-key-file>" -o BatchMode=yes rdump.py <user>@<build-server>:<work-dir>
./sv.sh "cd <work-dir> && python3 tools2/rdump.py stock/matt.ko $*"
