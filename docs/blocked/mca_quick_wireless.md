# mca_quick_wireless - BLOCKED before compile (scaffold only; reference = OS4 stock, 37 functions, 0 exports)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_strategy/strategy_wireless/mca_quick_wireless.c` (2493 lines) + `inc/mca_quick_wireless.h` (flattened); BUILD/defconfig generated, 14 OS4 stub providers (incl. `mca_basic_wireless`, `mca_smart_charge`).

OS3 -> OS4 stock: same function set, nearly all functions differ; new import `strategy_wireless_enable_cp_error_irq` (provider mca_basic_wireless) and the changed `mca_smartchg_if_ops_register` CRC.

First-compile blockers (42 distinct errors, 94 total) are all missing reconstructed public-header items whose VALUES must be read from the stock:
- events `MCA_EVENT_WIRELESS_{WLS_DEBUG,THERMAL_PHONE_FLAG,SW_SET_THERMAL_ICHG,SW_SET_QC_ICHG,MAGNETIC_QUIT_QC,AUDIO_PHONE_STS}`, `MCA_EVENT_CHARGE_ACTION`;
- strategy status types `STRATEGY_STATUS_TYPE_{WLS_MAGNET_LIMIT,QC_START_FLAG,QC_IBAT_MAX,JEITA_FFC_ITERM,CHARGING}`, `FG_IC_BASE/FG_IC_FLIP/FG_SITE_MAX`, `CP_MODE_FORWARD_{1_1,2_1,4_1}`, `CP_PMID_ERROR_*`, `MCA_QUICK_CHG_STS_*`, `MCA_BATT_CHGR_STATUS_*`;
- `struct wls_adapter_power_cap`, the partition structs listed in mca_smart_charge/STATUS.md;
- prototypes `strategy_class_wireless_ops_{set_parallel_charge,get_adapter_power,get_adapter_charger_mode}`, `strategy_class_wireless_op_get_rx_iout_limit`, `platform_class_wireless_notify_cp_status`,
  `platform_class_buckchg_ops_set_chg` (the OS4 exports of mca_basic_wireless / mca_platform_*_class give the real names and CRCs: `python3 <build-dir> exports <work-dir>`).

The jump-table decoding recipe used for mca_wireless_revchg (read the `.rodata` halfword table, map the targets to the case bodies) gives the event values.
