#!/usr/bin/env bash
# _g15.sh: push work/re/src/mitee_dlkm to <work-dir> and run the ThinLTO emulation vs stock (errors first)
cd "$(dirname "$0")/.." || exit 1
K="-i <ssh-key-file> -o BatchMode=yes -o ConnectTimeout=20"; U=<user>@<build-server>
tar c --exclude=STATUS.md --exclude=_g15.sh --exclude=BUILD.bazel mitee_dlkm | ssh $K $U "rm -rf <work-dir> tar x -C <work-dir>"
ssh $K $U "cd <work-dir> && DEFS='-DCONFIG_GH_MSGQ=1 -DCONFIG_GH_RM_DRV=1 -DCONFIG_QCOM_SECURE_BUFFER=1 -DCONFIG_QCOM_MINIDUMP=1 -DCONFIG_QCOM_SCM=1 -DCONFIG_XRING_OPTEE_SHM_NUM_PRIV_PAGES=1 -DCONFIG_XRING_OPTEE_BENCHMARK=1' INCS='' ./pb_ccl.sh mitee_dlkm mitee_dlkm -Wno-unused-variable -Wno-unused-function -Wno-unused-const-variable -Wno-implicit-fallthrough  2>&1 | tail -${TAILN:-60}; cp <work-dir> <work-dir>"
