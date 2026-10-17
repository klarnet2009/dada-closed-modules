# mca_smart_charge - BLOCKED before compile (scaffold only; reference = OS4 stock `mca_smart_charge.ko`, 35 functions, 7 exports)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_smart_charge/mca_smart_charge.c` + `mca_smart_charge.h` (copied flattened; BUILD.bazel/defconfig generated with `work/a17/portB/mkbuild.py`; providers = generated OS4 stubs `//re/stubs_a17B/os4`).

OS3 -> OS4 stock: same 35-function set, but 29 functions differ in bytes and the export CRC of `mca_smartchg_if_ops_register` changed (0xf068973d -> 0x85e2f9d0): the `struct mca_smartchg_if_ops` of `smart_chg_class.h` was extended in OS4.
The OS3 layout from the stock BTF (8 callbacks, 80 bytes) is in `stubs_a17B/include/mca/smartchg/smart_chg_class.h`; the new OS4 members must still be found from the code. The other 6 export CRCs are unchanged.

What is missing for a first compile (13 distinct errors left):
- the partition structs `smart_basp_header` (9 named fields checksum/type/total_len/jeita_ffc_term_size/jeita_normal_term_size/wired_ffc_size/wired_normal_size/wls_ffc_size/wls_normal_size, probably 10 ints),
  `smart_batt_spec` (`step_size`, flexible `steps[]`), `smart_batt_spec_curve`, `smart_batt_jeita_term_para` (public header `mca/shared_memory/charger_partition_class.h`, absent from every source tree: derive the layouts from the
  stock code, e.g. the inlined `smart_charge_cal_checksum`/`handle_baa_data` in `smart_charge_file_close`);
- `CHARGE_DFX_SMART_NAVIGATION_{TRIGGERED,SOC_ERR}`, `CHARGE_DFX_SMART_ENDURANCE_{TRIGGERED,SOC_ERR}` (values from the `mca_charge_mievent_report` immediates);
- `MCA_EVENT_CHARGE_STATUS` -> `MCA_EVENT_TYPE_CHARGE_STATUS`; `mca_smartchg_set_scene` returns void in the template (my `smart_chg_class.h` says int).

The stock has NO standalone `smart_charge_cal_checksum`/`handle_baa_data`/`handle_mmap_data` (inlined); otherwise the template function set matches.
Expected effort: header reconstruction ~20 symbols, then function-by-function adaptation of ~29 functions.

Shared header work by G2 (17.10.2026), valid for every module that includes `stubs_a17A`/`stubs_a17B`:
- `mca/smartchg/smart_chg_class.h` (both trees) now has the OS4 layout of `struct mca_smartchg_if_ops` (88 bytes, from the stock BTF of mca_smart_charge: +80 `is_support_pps_100w`, a `bool f(void)` callback implemented by `strategy_buckchg_is_support_pps_100w`, kCFI 0x36b1c5a6), `enum mca_smartchg_if_chg_type` with 6 values (`BUCK, QC, WL_QC, JEITA, WL_BASIC(guess), THERMAL = 5`, END = 6 because `mca_smartchg_if_ops_register()` accepts `type < 6`), `mca_smartchg_set_scene` returns void. The export CRC of `mca_smartchg_if_ops_register` (0x85e2f9d0) is NOT yet verified against this spelling (needs the genksyms brute force `work/re/g2_tools/enum_bf3.py`-style once mca_smart_charge compiles).
- `mca/common/mca_event.h` (both): `MCA_EVENT_WIRELESS_THERMAL_PHONE_FLAG = 0xf` (the immediate of the two `mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS=3 / BASIC_WIRELESS=2, 0xf, flag)` calls in the stock mca_charger_thermal), `MCA_EVENT_WIRELESS_EPP_MODE = 0x4e` (the stock process_event handles 0x4c and 0x4e with the same case body: which of them is EPP is a guess), `MCA_EVENT_TYPE_TYPEC_PORT_STATUS = 8` (proven by the stock mca_protocol_pd_class probe) and the a17B-only wireless values are now in both trees.
- The module-local BTF of the OS4 charging modules contains NO definition of the wireless enums (`WLS_CHIP_VENDOR_*`, `ADAPTER_*` of the wireless adapters, `XM_WLS_CHARGER_TYPE_*`, `DEBUG_SET_*FOD`): they live in the base BTF; their values can only be read from compare constants in the stock code (`nuvolta_1652_*`, `strategy_wireless_*`).
- Tools: `work/re/g2_tools/` (`jt.py` = jump-table decoder for switch tables: prints target -> case values, used to get event numbers; `varb.py`/`var.py` source-variant tester; `pin.py` `#line` pinner; `pad_g2.py`; `enum_bf3.py`).

G2: the OS4 `struct mca_smartchg_if_ops` is now known (88 bytes, above); the partition structs (`smart_basp_header`, `smart_batt_spec`, ...) are still to be derived (`START_ID=141904 python -I work/re/a17A_scripts/btf_dump.py local work/a17/portB/stock/mca_smart_charge.ko` gives member names for the module-local ones: dump saved by G2 in /tmp only, regenerate).
