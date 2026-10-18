#!/bin/bash
# fx.sh <func> : compact stock listing
python3 "$(dirname $0)/fn.py" "$(dirname $0)/${DUMP:-dump_mca_smart_charge}" $1 2>&1 | sed 's/  */ /g; s/^ *//' | grep -v -E "^[0-9a-f]+: (paciasp|autiasp|ret)|^[0-9a-f]+: (ldp|stp) x(19|2[0-9]|29), x(2[0-9]|30), \[sp"
