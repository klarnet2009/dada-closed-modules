#!/usr/bin/env bash
# _lay.sh <struct-name> [file.c]: record layout of `struct <name>` as compiled in the g12c emulation (run _g12.sh first to push the sources)
S="$1"; F="${2:-core.c}"
ssh -i "<ssh-key-file>" -o BatchMode=yes <user>@<build-server> "cd <work-dir> && MODNAME=mitee_dlkm ./pb_cc.sh <work-dir> /tmp/g12c_lay.o -DCONFIG_GH_MSGQ=1 -DCONFIG_GH_RM_DRV=1 -DCONFIG_QCOM_SECURE_BUFFER=1 -DCONFIG_QCOM_MINIDUMP=1 -DCONFIG_QCOM_SCM=1 -DCONFIG_XRING_OPTEE_SHM_NUM_PRIV_PAGES=4 -DCONFIG_XRING_OPTEE_BENCHMARK=1 -Xclang -fdump-record-layouts-simple 2>&1 | grep -v warning > /tmp/g12c_lay.txt; awk '/^Type: struct $S\$/{p=1;next} p&&/^Type:/{exit} p{print}' /tmp/g12c_lay.txt | head -\${HEADN:-80}"
