# mca_hwid - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS4 stock changes: (1) mca_get_hwid_info: the 7/2 country fix now comes first, then platform 10 (project 'donghai'): get_smem_battery_is_new_batt_sn(&is_new) (new import from mca_qcom_smem, depends=mca_log,hwid,mca_qcom_smem) and, when it is a new battery SN and hwid_value is in {0x10007,0x10008,0x20007,0x20008,0x90008,0x110008,0x120008,0x190008,0x210008,0x220008,0x290008}, product_name = "donghai"; platform 12 ('warsaw'/'yanan'): hwid_value in {8,9,0x10008,0x20008,0x90008} -> product_name = "yanan"; the log line now also prints product_name and hwid_value (#line pins 36/89/100). (2) new export `bool mca_is_warsaw_p2_before(const struct mca_hwid *)`: platform 12 and hwid_value in {1,8,9,0x10000,0x10001} (logs 'warsaw hwid_value: %#x'). New provider stub package work/re/src/stubs_g6 (mca_qcom_smem_stub = copy of the a17B os4 stub with OUT name mca_qcom_smem.ko so depends= matches; its export CRCs are the OS4 stock ones, forged struct tags). Real Kleaf build vs OS4 stock vendor_ramdisk/mca_hwid.ko: compare_ko.py EQUIVALENT (2 functions, 2 exports, 15 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_hwid - EQUIVALENT (port task)


Template: XRING tree work/re/xiaomi-modules/mca/mca_common/mca_hwid.c
Result: EQUIVALENT: compiles on 6.6.144; function set (1), bytes, relocs, export + CRC, imports + CRC (13), modinfo (depends mca_log,hwid), data identical

- the stock reads the two ADCs that are commented out in the template (product_adc = get_hw_project_adc(), build_adc = get_hw_build_adc()) and has a special case after product_name_get: platform_version == 7 && country_version == 2 -> country_version = 0
- one blank line before the multi-line mca_log_err pins __LINE__ (52)
- new provider stub hwid_stub + include/hwid.h (uint32_t getters, product_name_get; all 9 export CRCs verified against stock hwid.ko)
