#!/bin/bash
# sc.sh <func...>: symcmp.py stock vs the last built .ko (server <work-dir>)
D="/e/<workdir>/"; U=<user>@<build-server>
scp -q -i "$<ssh-key-file>" -o BatchMode=yes "$D/work/a17/review/tools3/symcmp.py" $U:<work-dir>
ssh -i "$<ssh-key-file>" -o BatchMode=yes $U "cd <work-dir> && sed -i 's#<work-dir>' symcmp.py && python3 -I symcmp.py ../stock/mca_basic_wireless.ko ../built/mca_basic_wireless.ko $* 2>&1 | cut -c1-${CW:-400} | head -${N:-60}"
