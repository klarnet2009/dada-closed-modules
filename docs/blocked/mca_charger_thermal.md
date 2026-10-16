# mca_charger_thermal - BLOCKED (does not compile yet; the XRING template is an older/smaller source than the OS4 stock)

SAFETY: charging module (`mca_*`). Source is for the community only, never to be flashed or put into a ROM build.

Reference binary: `<work-dir>` (28 functions; the OS3 stock has 27 and only 9 are identical to OS4), vermagic 6.6.57-android15-8.
Template: XRING `work/re/xiaomi-modules/mca/mca_strategy/strategy_charger/mca_charger_thermal.c` (1016 lines) + `inc/*.h`, copied here. The stock log lines go up to at least 1212 (`mca_charger_thermal_process_event` logs at 1172 and 1212), so the stock source is clearly longer than the template and differs in structure.

State: with the reconstructed MCA headers of `stubs_a17A` the template is down to 6 compile errors (instant loop `scripts/ccl.sh`):
- `MCA_EVENT_WIRELESS_THERMAL_PHONE_FLAG`, `MCA_EVENT_WIRELESS_EPP_MODE`: event ids unknown (process_event switch has 0x4e+ cases, jump table in the stock).
- `struct mca_smartchg_if_ops`, `MCA_SMARTCHG_IF_CHG_TYPE_THERMAL` (stock value 5), `mca_smartchg_if_ops_register()`: struct is 88 bytes = int type + void *data + 9 callback slots, `set_wls_super_sts` at +0x38; the import CRC of the register function changed OS3 -> OS4 (struct changed) and has to be forced with a genksyms pad (`a17A_scripts/padcrc.py`).
- two header names of the template (`mca/common/mca_smem.h`, `mca/smartchg/smart_chg_class.h`) are now wrapper headers in `stubs_a17A`.
Nothing was iterated on function bytes.
Helpers for the next round: BTF with member names of all module-local structs (`START_ID=141904 python -I work/re/a17A_scripts/btf_dump.py local work/re/a17A_stock/mca_charger_thermal.ko`: `mca_thermal_info`, `mca_thermal_data`, `mca_wireless_thermal_data`, `mca_thermal_ctrl_info`, `mca_buck_jeita_smartchg_data` and the 28 function prototypes), stock disassembly in `<work-dir>`.
Stock imports without stub yet: `mca_smartchg_if_ops_register`, `mca_get_hwid_info`.
