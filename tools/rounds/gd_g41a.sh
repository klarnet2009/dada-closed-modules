#!/usr/bin/env bash
# gd.sh CHIPS [opts]: push gx/gd, make dis of last built .o, compare final stores stock vs built
D="$(dirname "$0")"
scp -q -i "<ssh-key-file>" -o BatchMode=yes $D/gx.py $D/gd.py <user>@<build-server>:<work-dir>
bash $D/mkdis.sh ${MODN:-aw882xx_dlkm} >/dev/null
bash $D/sv.sh "cd <work-dir> && python3 gd.py $*"
