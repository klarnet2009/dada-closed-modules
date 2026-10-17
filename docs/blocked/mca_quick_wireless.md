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

Shared header work by G2 (17.10.2026), valid for every module that includes `stubs_a17A`/`stubs_a17B`:
- `mca/smartchg/smart_chg_class.h` (both trees) now has the OS4 layout of `struct mca_smartchg_if_ops` (88 bytes, from the stock BTF of mca_smart_charge: +80 `is_support_pps_100w`, a `bool f(void)` callback implemented by `strategy_buckchg_is_support_pps_100w`, kCFI 0x36b1c5a6), `enum mca_smartchg_if_chg_type` with 6 values (`BUCK, QC, WL_QC, JEITA, WL_BASIC(guess), THERMAL = 5`, END = 6 because `mca_smartchg_if_ops_register()` accepts `type < 6`), `mca_smartchg_set_scene` returns void. The export CRC of `mca_smartchg_if_ops_register` (0x85e2f9d0) is NOT yet verified against this spelling (needs the genksyms brute force `work/re/g2_tools/enum_bf3.py`-style once mca_smart_charge compiles).
- `mca/common/mca_event.h` (both): `MCA_EVENT_WIRELESS_THERMAL_PHONE_FLAG = 0xf` (the immediate of the two `mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS=3 / BASIC_WIRELESS=2, 0xf, flag)` calls in the stock mca_charger_thermal), `MCA_EVENT_WIRELESS_EPP_MODE = 0x4e` (the stock process_event handles 0x4c and 0x4e with the same case body: which of them is EPP is a guess), `MCA_EVENT_TYPE_TYPEC_PORT_STATUS = 8` (proven by the stock mca_protocol_pd_class probe) and the a17B-only wireless values are now in both trees.
- The module-local BTF of the OS4 charging modules contains NO definition of the wireless enums (`WLS_CHIP_VENDOR_*`, `ADAPTER_*` of the wireless adapters, `XM_WLS_CHARGER_TYPE_*`, `DEBUG_SET_*FOD`): they live in the base BTF; their values can only be read from compare constants in the stock code (`nuvolta_1652_*`, `strategy_wireless_*`).
- Tools: `work/re/g2_tools/` (`jt.py` = jump-table decoder for switch tables: prints target -> case values, used to get event numbers; `varb.py`/`var.py` source-variant tester; `pin.py` `#line` pinner; `pad_g2.py`; `enum_bf3.py`).

G2: decoded the OS4 `mca_wireless_quick_charge_process_event` jump table (`jt.py`): handled events 0, 1, 7, 8, 9, 0xb, 0xc, 0xd, 0x23, 0x25, 0x26, 0x2a, 0x30, 0x31, 0x33, 0x42, 0x47 (everything else default). The OS3-era numbers assumed by the template (`BATT_AUTH_PASS 0x28`, `BTB_CHANGE 0x27`, `ABNORMAL 0x32`, `VBAT_OVP 0x2c`, ...) are therefore NOT the cases of the OS4 module: the OS4 module handles a different event set (0x42 sets +0x35c and reads the soc via strategy_class_fg_ops_get_soc; 0x47 logs at line 758 and calls `mca_vote(+0x20, ..., 1, value)`). The template's case list cannot be kept; the switch has to be rebuilt from the stock handlers.
