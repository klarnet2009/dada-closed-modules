#!/usr/bin/env bash
# ob.sh [func...]: batch multiset diff of the ThinLTO emulation vs stock (MIN=n, V=1)
D="$(dirname "$0")"
scp -q -i "<ssh-key-file>" -o BatchMode=yes $D/opb.py <user>@<build-server>:<work-dir>
bash $D/sv.sh "cd <work-dir> && MOD='$MOD' MIN='$MIN' V='$V' python3 tools2/opb.py $*"
