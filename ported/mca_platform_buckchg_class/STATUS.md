# mca_platform_buckchg_class - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS4 stock adds one export `int platform_class_buckchg_ops_set_cp_vendor_id(unsigned int role, int *val)` (wrapper for the new 83rd op set_cp_vendor_id at ops+656; struct platform_class_buckchg_ops is now 664 bytes, register() export CRC changed to 0x129fc036). The module now builds against the OS4 header tree //re/stubs_g5:mca_headers (g5: same header verified by that CRC) instead of stubs_mcaA; the new wrapper is in mca_platform_buckchg_class.c (prototype local). Real Kleaf build vs OS4 stock vendor_ramdisk/mca_platform_buckchg_class.ko: compare_ko.py EQUIVALENT (86 functions, 84 exports, 3 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_platform_buckchg_class - EQUIVALENT (port task)


Template: XRING tree work/re/xiaomi-modules/mca/mca_platform/mca_platform_buckchg_class.c
Result: EQUIVALENT: compiles on 6.6.144; function set (85), bytes, relocs, 83 exports + CRC (incl. the 82-op struct platform_class_buckchg_ops reconstructed from the stock BTF), imports + CRC (3), modinfo (no depends), 3 data objects identical

- the stock is a different version of the wrapper set: template-only wrappers removed (platform_class_buckchg_is_init_ok, ops_get_adc_enable, ops_get_bcl_match_max_powercap, ops_get_real_type, ops_set_vac_pd, set_otg_en); stock-only wrappers added with the template pattern (ops_is_init_ok(role), ops_set_otg, ops_get_pack_ibat, ops_get_pack_tbat, ops_get_aicl_status, ops_set_restart_aicl, ops_set_too_hot_limit)
- header: enum buck_charger_role gets AUX_BUCK_CHARGER and MAX_BUCK_CHARGER (= 2: platform_buckchg_ops_data is 2 x 16 bytes)
