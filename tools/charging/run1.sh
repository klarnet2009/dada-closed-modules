#!/usr/bin/env bash
# waits for load < 20 then builds + compares (one Bazel build at a time)
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"
cd "/e/<workdir>/work/re/src" || exit 1
for i in $(seq 1 120); do
  L=$(ssh -i "<ssh-key-file>" -o BatchMode=yes -o ConnectTimeout=20 <user>@<build-server> "cut -d' ' -f1 /proc/loadavg" 2>/dev/null)
  echo "load $L"; [ -n "$L" ] && [ "${L%.*}" -lt 20 ] && break; sleep 30
done
SYMFN="$SYMFN" bash ../g22e_tools/kb.sh bq27z561 bq27z561 bq27z561 stubs_g14g
