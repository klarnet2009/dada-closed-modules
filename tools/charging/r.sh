#!/bin/bash
# r.sh <func> <from_hex> <to_hex>: raw annotated listing of a range (REGBASE set per function), frame boilerplate dropped
f=$1; shift
REGBASE=${REGBASE:-x19:560,x0:560} bash "$(dirname $0)/a.sh" $f "$@" | grep -v -E "^[0-9a-f]+: (\.word|paciasp|autiasp)" | cut -c1-${W:-200}
