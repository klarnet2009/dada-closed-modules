#!/bin/bash
# v.sh <func...>: annotated+summarized stock listing without frame boilerplate
for f in "$@"; do echo "=== $f"; bash "$(dirname $0)/a.sh" $f | python3 "$(dirname $0)/sum.py" | grep -v -E "^[0-9a-f]+: (stp|ldp|sub sp|add sp|mov x29|add x29|ldr x[0-9]+, \[sp|str x[0-9]+, \[sp|mrs|\.word|paciasp|autiasp|stur x[0-9]+, \[x29|ldur x[0-9]+, \[x29)" | cut -c1-${W:-175}; done
