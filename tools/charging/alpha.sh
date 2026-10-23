# usage: alpha.sh module   -> for each differing function writes rep/al_<m>__<f>.txt and prints summary
G=<work-dir> m=$1
cp <work-dir> $G/bin/alpha.py 2>/dev/null
fl=$(grep -E "^FAIL function-bytes" $G/rep/cmp_$m.txt | sed -E 's/^[^:]*: //; s/,//g')
mkdir -p $G/lst
for f in $fl; do
  ( ulimit -v 16000000; timeout 300 python3 -I $G/bin/lst2.py $G/st/gl_$m.ko $f > $G/lst/S_${m}__$f.txt 2>$G/lst/err_S.txt;
    timeout 300 python3 -I $G/bin/lst2.py $G/built/$m.ko $f > $G/lst/B_${m}__$f.txt 2>$G/lst/err_B.txt;
    python3 -I $G/bin/alpha.py $G/lst/S_${m}__$f.txt $G/lst/B_${m}__$f.txt > $G/rep/al_${m}__$f.txt 2>&1 )
  echo "$m $f hunks=$(grep -c '^==' $G/rep/al_${m}__$f.txt) $(tail -1 $G/rep/al_${m}__$f.txt)"
done
