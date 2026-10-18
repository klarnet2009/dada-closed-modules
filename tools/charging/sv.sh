#!/bin/bash
exec ssh -i "<ssh-key-file>" -o BatchMode=yes -o ConnectTimeout=20 <user>@<build-server> "$@"
