G=<work-dir> m=$1
for S in $G/lst/S_${m}__*.txt; do f=$(basename $S .txt); f=${f#S_${m}__}
 python3 -I $G/bin/alpha3.py $S $G/lst/B_${m}__$f.txt > $G/rep/a3_${m}__$f.txt 2>&1
 echo "$m $f $(tail -1 $G/rep/a3_${m}__$f.txt)"; done
