#!/bin/bash
# mkpkg.sh <module> [extra deps e.g. //re/stubs_g1:g1_headers]: create BUILD.bazel + <module>_defconfig (ThinLTO) in work/re/src/<module>
cd "$(dirname "$0")/.." || exit 1
m=$1; shift; mkdir -p $m
printf '# CONFIG_LTO_NONE is not set\nCONFIG_LTO_CLANG_THIN=y\n' > $m/${m}_defconfig
deps='"//common:all_headers_aarch64"'; for d in "$@"; do deps="\"$d\", $deps"; done
cat > $m/BUILD.bazel <<EOT
load("/<build-dir>", "ddk_module")

ddk_module(
    name = "$m",
    srcs = glob(["*.c", "*.h"]),
    out = "$m.ko",
    defconfig = "${m}_defconfig",  # stock was built with CONFIG_LTO_CLANG_THIN
    copts = ["-ffunction-sections", "-fdata-sections"],
    local_defines = ["CONFIG_LTO=1"],
    visibility = ["//re:__subpackages__"],
    kernel_build = "//common:kernel_aarch64",
    deps = [$deps],
)
EOT
