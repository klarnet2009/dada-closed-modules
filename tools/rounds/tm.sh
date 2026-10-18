#!/usr/bin/env bash
# tm.sh [funcs...]: mitee_dlkm emulation + multiset diff (DEFS of the module set here)
export DEFS='-DCONFIG_GH_MSGQ=1 -DCONFIG_GH_RM_DRV=1 -DCONFIG_QCOM_SECURE_BUFFER=1 -DCONFIG_QCOM_MINIDUMP=1 -DCONFIG_QCOM_SCM=1 -DCONFIG_XRING_OPTEE_SHM_NUM_PRIV_PAGES=1 -DCONFIG_XRING_OPTEE_BENCHMARK=1'
export XF='-Wno-unused-variable -Wno-unused-function -Wno-unused-const-variable -Wno-implicit-fallthrough'
exec bash "$(dirname "$0")/t.sh" mitee_dlkm "$@"
