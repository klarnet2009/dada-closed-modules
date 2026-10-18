#!/bin/bash
# builds <work-dir> from the CAF macro file (constants kept verbatim) + our own definition
P=<work-dir>
head -2275 $P/wlan_qcacld3_modules.bzl | grep -v 'load("/<build-dir>"' | grep -v 'load(":target_variants.bzl"' | sed 's#^load("/<build-dir>", "ddk_module")#load("/<build-dir>", "ddk_module")#' > $P/g4_modules.bzl
cat >> $P/g4_modules.bzl <<'EOT'

# : one DDK module per chipset on our GKI kernel (stubs with forged OS4 CRCs instead of the real platform modules)
_g4_provs = ["cfg80211", "cnss2", "cnss_nl", "cnss_prealloc", "cnss_utils", "hwid", "icnss2", "ipam", "qcom_iommu_util",
             "qcom_va_minidump", "qmi_helpers", "rproc_qcom_common", "sched_walt"]

def g4_module(chipset, extra_local_defines = []):
    hw = _chipset_hw_map[chipset]
    ipaths = _chipset_header_map[chipset] + _hw_header_map[hw] + _fixed_ipaths
    iglobs = []
    for i in _fixed_includes:
        iglobs.append(i)
    for i in ipaths:
        iglobs.append("{}/*.h".format(i))
        iglobs.append("{}/**/*.h".format(i))
    for i in _private_ipaths:
        iglobs.append("{}/*.h".format(i))
        iglobs.append("{}/**/*.h".format(i))
    copts = []
    for i in _fixed_includes:
        copts.append("-include")
        copts.append(i)
    copts += ["-Wunused-but-set-parameter", "-include", "configs/grep_defines_g4.h"]
    srcs = native.glob(iglobs) + _fixed_srcs + ["configs/grep_defines_g4.h"]
    out = "qca_cld3_{}.ko".format(chipset.replace("-", "_"))
    ddk_module(
        name = "qca_cld3_{}".format(chipset.replace("-", "_")),
        srcs = srcs,
        includes = ipaths + ["."],
        kconfig = "Kconfig",
        defconfig = "g4_{}_defconfig".format(chipset),
        local_defines = extra_local_defines,
        conditional_srcs = _conditional_srcs,
        copts = copts,
        out = out,
        kernel_build = "//common:kernel_aarch64",
        deps = ["//common:all_headers_aarch64", "//re/qcawlan/platform_inc:wlan-platform-headers", "//re/qcawlan/dataipa_inc:include_headers"] +
               ["//re/stubs_g4wlan:prov_{}".format(p) for p in _g4_provs],
    )
EOT
cat > $P/BUILD.bazel <<'EOT'
load(":g4_modules.bzl", "g4_module")
package(default_visibility = ["//visibility:public"])
exports_files(glob(["configs/*"]))
g4_module("qca6750")
g4_module("wcn7750", ["CONFIG_IPA3=1"])
g4_module("peach", ["CONFIG_IPA3=1"])
g4_module("peach-v2", ["CONFIG_IPA3=1"])
g4_module("kiwi-v2", ["CONFIG_IPA3=1"])
EOT
for c in kiwi-v2 peach peach-v2 qca6750 wcn7750; do (cat $P/configs/sun_gki_${c}_defconfig; echo "# CONFIG_LTO_NONE is not set"; echo "CONFIG_LTO_CLANG_THIN=y") > $P/g4_${c}_defconfig; done
cat > $P/../platform_inc/BUILD.bazel <<'EOT'
load("/<build-dir>", "ddk_headers")
package(default_visibility = ["//visibility:public"])
ddk_headers(name = "wlan-platform-headers", hdrs = glob(["*.h", "net/*.h", "soc/qcom/*.h"]), includes = [".", "net"])
EOT
cat > $P/../dataipa_inc/BUILD.bazel <<'EOT'
load("/<build-dir>", "ddk_headers")
package(default_visibility = ["//visibility:public"])
ddk_headers(name = "include_headers", hdrs = glob(["drivers/platform/msm/include/linux/*.h", "drivers/platform/msm/include/uapi/linux/*.h"]),
            includes = ["drivers/platform/msm/include", "drivers/platform/msm/include/linux", "drivers/platform/msm/include/uapi"])
EOT
# stubs
rm -rf <build-dir> cp -r <work-dir> <build-dir>
cd <build-dir> for f in prov_sched-walt.c; do [ -f $f ] && mv $f prov_sched_walt.c; done; sed -i 's/prov_sched-walt/prov_sched_walt/g' BUILD.bazel; ls
