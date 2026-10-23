#!/usr/bin/env bash
# cyc.sh <func> : rebuild aw, show diff count, dump ours, ndiff vs stock (stock dump must exist in scratch g15c/<func>_stock.txt)
S="<scratch>"
D="$(dirname "$0")"
M=${M:-aw882xx_dlkm}; A=${A:-aw}
bash $D/aq.sh $A 2>&1 | grep -iE "error"
bash $D/ao.sh $A $1 2>&1 | grep -E "$1"
bash $D/sv.sh "cd <work-dir> && python3 tools2/dis.py $M $1 built" > $S/$1_ours.txt
python3 $S/ndiff.py $S/$1_stock.txt $S/$1_ours.txt ${C:-2} rn | head -${H:-200}
