#!/usr/bin/env bash
# strdiff.sh <mod> : strings only in stock / only in the last Kleaf build (server g15c)
bash "$(dirname "$0")/sv.sh" "cd <work-dir> for k in stock built; do strings -a -n 3 \$k/$1.ko | sort -u > o/str_\$k.txt; done; echo '--- only stock'; comm -23 o/str_stock.txt o/str_built.txt | cut -c1-110 | head -\${N:-60}; echo '--- only built'; comm -13 o/str_stock.txt o/str_built.txt | cut -c1-110 | head -\${N:-60}"
