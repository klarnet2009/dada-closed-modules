# mca_platform_buckchg_class - EQUIVALENT (port task)

Template: XRING tree work/re/xiaomi-modules/mca/mca_platform/mca_platform_buckchg_class.c
Result: EQUIVALENT: compiles on 6.6.144; function set (85), bytes, relocs, 83 exports + CRC (incl. the 82-op struct platform_class_buckchg_ops reconstructed from the stock BTF), imports + CRC (3), modinfo (no depends), 3 data objects identical

- the stock is a different version of the wrapper set: template-only wrappers removed (platform_class_buckchg_is_init_ok, ops_get_adc_enable, ops_get_bcl_match_max_powercap, ops_get_real_type, ops_set_vac_pd, set_otg_en); stock-only wrappers added with the template pattern (ops_is_init_ok(role), ops_set_otg, ops_get_pack_ibat, ops_get_pack_tbat, ops_get_aicl_status, ops_set_restart_aicl, ops_set_too_hot_limit)
- header: enum buck_charger_role gets AUX_BUCK_CHARGER and MAX_BUCK_CHARGER (= 2: platform_buckchg_ops_data is 2 x 16 bytes)
