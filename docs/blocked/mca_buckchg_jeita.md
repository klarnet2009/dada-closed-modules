# mca_buckchg_jeita - BLOCKED (does not compile yet; template older than the OS4 stock)

SAFETY: charging module (`mca_*`). Source is for the community only, never to be flashed or put into a ROM build.

Reference binary: `<work-dir>` (17 functions; OS3 has 15, only 4 identical), vermagic 6.6.57-android15-8.
Template: XRING `work/re/xiaomi-modules/mca/mca_strategy/strategy_charger/mca_buckchg_jeita.c` (1052 lines) + `inc/*.h`, copied here.
Stock additions visible in symtab/imports: `mca_jeita_smartchg_delta_fv_callback`, `mca_buckchg_jeita_smartchg_update_baa_para`, `mca_buckchg_jeita_smartchg_baa_update_jeita_data`, static `g_jeita_smartchg_if_ops` (88 bytes, callbacks at +0x10 and +0x48), new imports `platform_fg_ops_get_batt_cell_info`, `mca_is_warsaw_p2_before`, `protocol_class_get_adapter_type`, `mca_smartchg_is_extreme_cold_enabled`, `mca_smartchg_if_ops_register`, `of_get_child_by_name`.

State: with the `stubs_a17A` headers the template stops at 23+ compile errors: missing `STRATEGY_STATUS_TYPE_CHARGING/JEITA_FFC_ITERM/JEITA_NORMAL_VTERM/JEITA_FFC_SIZE`, `MCA_QUICK_CHG_STS_*`, `CountryCN`, `struct smart_batt_jeita_term_para` and the smart-charge (baa) structs/ops. They come from public headers (`mca/smartchg/smart_chg_class.h`, the strategy status enum) that exist in none of our trees; they must be recovered from the stock BTF (`START_ID=141904 python -I work/re/a17A_scripts/btf_dump.py local work/re/a17A_stock/mca_buckchg_jeita.ko`, module-local structs have member names) and the switch tables of the stock. No function bytes were compared.
