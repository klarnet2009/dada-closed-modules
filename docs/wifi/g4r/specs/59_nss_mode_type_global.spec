# g4r 59: the stock has a GLOBAL 4-byte bss object `nss_mode_type` right behind the local `g_nss_mode_type` (.bss+0x10e308/+0x10e30c in peach_v2), referenced by nothing
# in the code (only the local one is used). Added as an unused global (compare_ko_v2 objects: only-stock nss_mode_type).
@@@ xiaomi_ext/xiaomi_ext.c
<<<<
static int g_nss_mode_type;

====
static int g_nss_mode_type;
int nss_mode_type;

>>>>
# g4r 59b: qca6750 / wcn7750 stock have NO soft-unload feature (no "WiFi is disabled" / "Initiating WLAN idle shutdown" strings, no cnss_wlan_hw_enable import):
# CONFIG_CNSS_HW_SECURE_DISABLE is removed for those two chips (only-ours hdd_disable_wifi 452 B and hdd_get_wlan_driver_status 12 B disappear).
@@@ BUILD.bazel
<<<<
g4_module("qca6750", ["CONFIG_QCOM_VA_MINIDUMP=1", "CONFIG_QCOM_MINIDUMP=1", "CONFIG_SCHED_WALT=1", "CONFIG_QCOM_IOMMU_UTIL=1", "__ANDROID_COMMON_KERNEL__=1", "CONFIG_CNSS_HW_SECURE_DISABLE=1", "CONFIG_QCOM_RPROC_COMMON=1"])
====
g4_module("qca6750", ["CONFIG_QCOM_VA_MINIDUMP=1", "CONFIG_QCOM_MINIDUMP=1", "CONFIG_SCHED_WALT=1", "CONFIG_QCOM_IOMMU_UTIL=1", "__ANDROID_COMMON_KERNEL__=1", "CONFIG_QCOM_RPROC_COMMON=1"])
>>>>
<<<<
g4_module("wcn7750", ["CONFIG_IPA3=1", "CONFIG_QCOM_VA_MINIDUMP=1", "CONFIG_QCOM_MINIDUMP=1", "CONFIG_SCHED_WALT=1", "CONFIG_QCOM_IOMMU_UTIL=1", "__ANDROID_COMMON_KERNEL__=1", "CONFIG_CNSS_HW_SECURE_DISABLE=1", "CONFIG_QCOM_RPROC_COMMON=1"])
====
g4_module("wcn7750", ["CONFIG_IPA3=1", "CONFIG_QCOM_VA_MINIDUMP=1", "CONFIG_QCOM_MINIDUMP=1", "CONFIG_SCHED_WALT=1", "CONFIG_QCOM_IOMMU_UTIL=1", "__ANDROID_COMMON_KERNEL__=1", "CONFIG_QCOM_RPROC_COMMON=1"])
>>>>
