#!/usr/bin/env bash
# kbw.sh <args of kb.sh>: wait until the server 1-min load average < 20, then run kb.sh (one Bazel build at a time via flock on <work-dir>)
K="-i <ssh-key-file>"
while :; do
  L=$(ssh -i "<ssh-key-file>" -o BatchMode=yes -o ConnectTimeout=20 <user>@<build-server> "cut -d' ' -f1 /proc/loadavg" 2>/dev/null)
  [ -n "$L" ] && [ "${L%.*}" -lt 20 ] && break
  sleep 45
done
echo "load $L at start"
exec bash "$(dirname "$0")/kb.sh" "$@"
