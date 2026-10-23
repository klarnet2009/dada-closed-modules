#!/usr/bin/env bash
# kbs.sh <pkg> <target> [stockname]: build //re/<pkg>:<target>, compare with GLOBAL stock; scratch <work-dir>
set -uo pipefail
pkg=$1; t=$2; s=${3:-$2}; W=<build-dir>; T=<work-dir>
[ "$(df --output=avail -BG / | tail -1 | tr -dc 0-9)" -ge 4 ] || { echo ABORT disk; exit 3; }
mkdir -p $T/built $T/rep
cd $W
BZ="tools/bazel --output_user_root=$W/.bazel_root"
nice -n 10 $BZ build --lto=none --config=stamp //re/$pkg:$t 2>&1 | grep -E 'error|ERROR|undefined|Build completed|fatal' | cut -c1-300 | head -20
P=$($BZ cquery --lto=none --config=stamp --output=files //re/$pkg:$t 2>/dev/null | head -1)
[ -n "$P" ] && [ -f "$P" ] || { echo NO KO; exit 1; }
cp "$P" $T/built/$t.ko; md5sum $T/built/$t.ko
cd $T/bin && python3 -I compare_ko_v2.py -v ../st/gl_$s.ko ../built/$t.ko > ../rep/${t}_cmp.txt 2>&1
grep -E '^(PASS|FAIL|VERDICT)' ../rep/${t}_cmp.txt | cut -c1-${CW:-300}
python3 -I preamble_cmp.py ../st/gl_$s.ko ../built/$t.ko 2>&1 | cut -c1-300 | tail -3
python3 -I - ../rep/${t}_cmp.txt > ../rep/${t}_funcs.txt <<'PY'
import sys,re
names=[]
for l in open(sys.argv[1]):
    m=re.match(r'FAIL function-(bytes|relocs)\s+\d+ of \d+ differ: (.*)$',l.rstrip())
    if m:
        for n in m.group(2).split(', '):
            n=n.strip()
            if n and n not in names and not n.startswith('...'): names.append(n)
print(' '.join(names))
PY
FN=$(cat ../rep/${t}_funcs.txt); echo "differing: $(echo $FN | wc -w): $FN"; FN="${SYMFN:-}"  # g30e: symcmp4 only on the functions named in SYMFN (a full run took 61 GB RAM on fg_comp)
[ -n "$FN" ] && (ulimit -v 16000000; nice -n 10 python3 -I symcmp4.py ../st/gl_$s.ko ../built/$t.ko $FN > ../rep/${t}_sym.txt 2>&1)
grep -E '^===' ../rep/${t}_sym.txt | cut -c1-200
P2=$W/bazel-out/k8-fastbuild/bin/re/$pkg; [ -d $P2 ] && { chmod -R u+w $P2; rm -rf $P2; echo cleaned; }; rm -rf $W/re/$pkg; df -h / | tail -1
