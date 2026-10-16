# mca_hwid - EQUIVALENT (agent mcaA, port task)

Template: XRING tree work/re/xiaomi-modules/mca/mca_common/mca_hwid.c
Result: EQUIVALENT: compiles on 6.6.144; function set (1), bytes, relocs, export + CRC, imports + CRC (13), modinfo (depends mca_log,hwid), data identical

- the stock reads the two ADCs that are commented out in the template (product_adc = get_hw_project_adc(), build_adc = get_hw_build_adc()) and has a special case after product_name_get: platform_version == 7 && country_version == 2 -> country_version = 0
- one blank line before the multi-line mca_log_err pins __LINE__ (52)
- new provider stub hwid_stub + include/hwid.h (uint32_t getters, product_name_get; all 9 export CRCs verified against stock hwid.ko)
