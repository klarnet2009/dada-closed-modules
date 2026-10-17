# mca_strategy_quickchg - BLOCKED before compile (scaffold only; reference = OS4 stock)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_strategy/strategy_charger/mca_strategy_quickchg.c` (4220 lines) + `inc/mca_quick_charger.h` (copied next to the .c); BUILD/defconfig generated, 18 OS4 stub providers.

OS3 -> OS4 stock: 13 functions that were inlined in OS3 are standalone in OS4 (`mca_quick_charge_{delay_enable_pmic_work,get_vstep,ovp_monitor_work,parse_chg_mode_info,parse_drop_fcc_para,parse_dt_override,
parse_enable_cp_para,parse_pmic_single_cp_charging_info,parse_third_pps_power_info,parse_vfc_info,parse_vstep_ex_para,parse_vstep_para,update_jeita_fcc}`), almost every other function differs, many imports changed.

Compile blockers (36 distinct errors with the headers present): same classes as in mca_quick_wireless/STATUS.md (smart_batt_spec partition structs, strategy/status/event enumerators with unknown values,
missing prototypes of mca_strategy_class / platform_cp / platform_fg_ic_ops / protocol_pd extras), plus `mca/smartchg/smart_chg_class.h` (reconstructed in stubs_a17B for the OS3 layout of `mca_smartchg_if_ops`).

Biggest module of the group; do it last, after mca_smart_charge, mca_wireless_revchg and mca_protocol_pd_class have provided the shared headers.

G2: `stubs_a17B` now has the OS4 `struct mca_smartchg_if_ops` layout (88 bytes) and the `mca_smartchg_set_scene` void prototype, see mca_charger_thermal/STATUS.md.
