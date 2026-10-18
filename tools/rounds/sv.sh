#!/usr/bin/env bash
# sv.sh <remote command> : run on the build server (key by absolute path)
exec ssh -i "<ssh-key-file>" -o BatchMode=yes -o ConnectTimeout=20 <user>@<build-server> "$@"
