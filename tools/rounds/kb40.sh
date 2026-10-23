#!/usr/bin/env bash
# kb40.sh <module...> (server side, run from <work-dir>): sequential Kleaf builds of <build-dir> (copied from $T/src/<m>), compare vs GLOBAL stock, md5, clean bazel-out
T=<work-dir> W=<build-dir>
for m in "$@"; do
  while :; do l=$(cut -d' ' -f1 /proc/loadavg); [ "${l%.*}" -lt 20 ] && break; sleep 60; done
  [ "$(df --output=avail -BG / | tail -1 | tr -dc 0-9)" -ge 4 ] || { echo ABORT disk; exit 3; }
  rm -rf $W/re/${m}_g40f; cp -r $T/src/$m $W/re/${m}_g40f
  cd $W; BZ="tools/bazel --output_user_root=$W/.bazel_root"
  nice -n 10 $BZ build --lto=none --config=stamp //re/${m}_g40f:$m > $T/res/$m.build 2>&1
  grep -E 'error|ERROR|undefined|Build completed|fatal' $T/res/$m.build | cut -c1-300 | head -8
  P=$($BZ cquery --lto=none --config=stamp --output=files //re/${m}_g40f:$m 2>/dev/null | head -1)
  if [ -n "$P" ] && [ -f "$P" ]; then
    mkdir -p $T/built; cp "$P" $T/built/$m.ko; md5sum $T/built/$m.ko
    (cd $T/tools2 && python3 -I compare_ko_v2.py ../stock/$m.ko ../built/$m.ko > ../res/$m.cmp 2>&1; python3 -I preamble_cmp.py ../stock/$m.ko ../built/$m.ko > ../res/$m.pre 2>&1)
    echo "== $m done"
  else echo "== $m NO KO"; fi
  P2=$W/bazel-out/k8-fastbuild/bin/re/${m}_g40f; [ -d $P2 ] && { chmod -R u+w $P2; rm -rf $P2; }; rm -rf $W/re/${m}_g40f
done
echo ALLDONE
