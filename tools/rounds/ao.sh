#!/usr/bin/env bash
# ao.sh aw|tfa|sia [funcs...] : ob.sh with env (MIN, V)
case $1 in aw) export MOD=aw882xx_dlkm;; tfa) export MOD=tfa98xx_dlkm;; sia) export MOD=sia91xx_dlkm;; esac
shift
exec bash "$(dirname "$0")/ob.sh" "$@"
