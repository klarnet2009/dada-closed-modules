#!/usr/bin/env bash
# aq.sh aw|tfa|sia : q.sh with the right env. prints compile errors only unless V=1
case $1 in aw) M=aw882xx_dlkm; D="-DCONFIG_MIEV=1 -DCONFIG_DEBUG_FS=1 -Wno-incompatible-pointer-types -Wno-visibility";; tfa) M=tfa98xx_dlkm; D="${TD:-}";; sia) M=sia91xx_dlkm; D="-DCONFIG_MIEV=1 -DSIA91XX_TYPE=1 -DSIPA_COMPILE_TO_MODULE=1 -include sia_compat.h -Wno-unused-function -Wno-incompatible-pointer-types";; esac
export DEFS="$D"
if [ -n "$V" ]; then TAILN=${TAILN:-25} bash "$(dirname "$0")/q.sh" $M $M; else TAILN=2000 bash "$(dirname "$0")/q.sh" $M $M 2>&1 | grep -E "error|Error|warning:|total masked" | head -${N:-30}; fi
