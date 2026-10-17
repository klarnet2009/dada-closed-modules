# mca_strategy_class - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS4 stock adds one export, `bool mca_strategy_is_support_pps_100w(void)` (= mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE, 27, &v); return v == 1; the inlined call has no NULL check on temp_data, as in the stock). Added to mca_strategy_class.c (prototype local to the .c, shared header stubs_mcaB untouched; export CRC matches with bool). Real Kleaf build vs OS4 stock vendor_ramdisk/mca_strategy_class.ko: compare_ko.py EQUIVALENT (7 functions, 5 exports, 4 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_strategy_class - STATUS: EQUIVALENT


Template: XRING `mca/mca_strategy/mca_strategy_class.c` unchanged.  Header `strategy_class.h` (stubs_mcaB) got `STRATEGY_FUNC_TYPE_CONNECTOR_ANTIBURN = 10` and
`STRATEGY_FUNC_TYPE_MAX` (the stock array `g_mca_stg_func` is 352 bytes = 11 entries of 32 bytes).
Real build + compare_ko: 6 functions identical, relocations, objects, 4 exports with CRCs, 3 imports, modinfo -> EQUIVALENT (first build).
