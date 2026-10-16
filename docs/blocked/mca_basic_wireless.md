# mca_basic_wireless - BLOCKED (triage only: 73 functions, 4000-line template that does not compile)

SAFETY: charging module (`mca_*`). Source is for the community only, never to be flashed or put into a ROM build.

Reference binary: `<work-dir>` (73 functions, 141 imports; the OS3 stock has 72 functions, only 26 identical to OS4), vermagic 6.6.57-android15-8.
Template: XRING `work/re/xiaomi-modules/mca/mca_strategy/strategy_wireless/mca_basic_wireless.c` (3996 lines) + `inc/mca_basic_wireless.h`, `mca_quick_wireless.h`, `mca_wireless_revchg.h`, copied here.
The OS4 module also has `strategy_wireless_smartchg_delta_fv_callback`, statics `g_strategy_wireless_if_ops` (112 bytes) and `g_basic_smartchg_if_ops` (88 bytes), imports `mca_charge_if_ops_register`, `mca_smartchg_if_ops_register` (CRC changed vs OS3), `platform_class_cp_enable_vbus_errorhi/lo` (new in OS4), `mca_parse_dts_u8_array`.

State: does not compile with the `stubs_a17A` headers: `struct wls_adapter_power_cap` is incomplete (member of the main struct in inc/mca_basic_wireless.h), the `mca/common/mca_adsp_glink.h` wrapper was added, and all smartchg types are missing (see the mca_charger_thermal / mca_buckchg_jeita STATUS). `scripts/miss.py` lists 16 imports without prototype in our headers: platform_class_cp_enable_vbus_errorhi/lo, platform_class_buckchg_ops_set_aicl_enable/set_boost_enable/set_wls_vdd_flag/set_chg/get_wls_curr/set_term_volt/set_wls_hiz/set_hiz/set_wls_input_curr_lmt/get_otg_boost_src, mca_wireless_rev_set_firmware_state (the pd_class and parse_dts ones are done by now). No function bytes were compared.
A full port of this module is a multi-day job on its own (73 functions, 13000 instructions).
