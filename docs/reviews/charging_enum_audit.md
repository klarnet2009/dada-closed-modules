# Charging enum audit (goal-G13d, task 0, 18.10.2026)

Question: the shared stubs headers `mca/common/mca_event.h` and `mca/common/mca_charge_mievent.h` (stubs_a17A/a17B/g2h/g2i/g2j/g2k/g5/mcaA/mcaB/portB/misc) carry enumerator names/values that differ from the OS4 stock BTF. Is any enumerator used as a passed/stored id (mievent id, uevent/notify id, case label) with a WRONG NUMBER or a WRONG NAME in the FINISHED charging modules?

Charging modules stay community-only: never flash, never in a ROM build, never in the kernel package. No claim of safety is made here.

## Method
1. Stock BTF of ALL OS4 charging modules (`mca_*`, bq27z561, nuvolta_1652, sc8581_sy6541, charger_partition, hl7603; start id 141904, local mode) dumped into `work/a17/review/g13d/btf/*.txt` (104 files). All enumerators of all stock modules were merged (`enumaudit.py`).
2. Every enum in every `work/re/src/stubs_*/include` header compared by NAME against the stock values (`enum_mismatch_by_tree.txt`: 178 mismatching enumerators, mostly `charge_dfx_ele` 104 and `mca_event_ele` 31, plus firmware_update_state, cp_int_stat_channel, cp_ovpgate_type, strategy_status_type, cp_role).
3. For each finished module: identifiers used in its sources, resolved through the header trees named in its BUILD.bazel, compared with the stock value and (reverse) with the stock NAME at the compiled value (`modaudit.py`, `modaudit2.py`; outputs `module_enum_usage_mismatch.txt`, `module_enum_value_mismatch_allenums.txt`).
4. The immediates in the built `.ko` equal the stock ones (previous reviews, compare_ko_v2 function-bytes / jump-table PASS for these functions), so a name/value disagreement can only be a WRONG NAME (the number is right).

## Result
**No finished module passes a wrong NUMBER.** Wrong NAMES (value right, name = another stock enumerator, i.e. a misleading source) were found in exactly two modules and are FIXED; the rest are harmless G2-spelling aliases of the correct stock name.

### Wrong names that were fixed (G13d)
Both modules now build with `work/re/src/stubs_g13d_g5` / `stubs_g13d_g2k` (copies of stubs_g5 / stubs_g2k whose two headers are the stock BTF enums, no G2 compat names: a missed old name fails to compile). After the rename the Kleaf `.ko` has IDENTICAL disassembly + relocations and identical section contents to the previous build (only the 20-byte build-id note differs), and compare_ko_v2 gives the same verdict (business 41/45 identical, fg_comp 79/97 identical, same differing functions), kCFI preamble 37/75 PASS.

| module | old source name (value in old header = stock immediate) | stock BTF name of that value |
|---|---|---|
| mca_business_charger_comp | `MCA_EVENT_CP_VBUS_OVP` (0x17=23) | `MCA_EVENT_CP_VUSB_OVP` |
| | `MCA_EVENT_CP_IBUS_OCP` (0x19=25) | `MCA_EVENT_CP_VBUS_OVP` |
| | `MCA_EVENT_CP_VUSB_OVP` (0x1e=30) | `MCA_EVENT_CP_IBUS_OCP` |
| | `CHARGE_DFX_CP_VBUS_OVP` (9) | `CHARGE_DFX_CP_VAC_OVP` |
| | `CHARGE_DFX_CP_IBUS_OCP` (5) | `CHARGE_DFX_CP_VBUS_OVP` |
| | `CHARGE_DFX_CP_VAC_OVP` (6) | `CHARGE_DFX_CP_IBUS_OCP` |
| | `MCA_EVENT_CP_REVERT_CHANGE` (46), `_DEBUG_*` (76..79), `_HANDLE_STATE` (60), `_SET_REVCHG_BCL` (59) | `MCA_EVENT_QUICK_REVCHG_CHANGE`, `MCA_EVENT_DEBUG_CTRL_*`, `MCA_EVENT_HANDLE_ALLOW_CHARGE`, `MCA_EVENT_REVCHG_BCL` |
| | local `#define MCA_EVENT_HW_INFO_0X53`, literal `case 0x2f`/`0x53`, `mievent_report(0x2d)`, `(0x39)` | `MCA_EVENT_SBU_LPD_CHANGE`, `MCA_EVENT_BOOST_STS`, `CHARGE_DFX_CHARGER_BROKEN`, `CHARGE_DFX_CP_ABSENT` |
| mca_strategy_fg_comp | `CHARGE_DFX_BATTERY_AUTH_FAIL_PARALLEL` (23), `_OCD_COUNT` (39), `_CUV_COUNT` (40), `_HSCD_COUNT` (41), `XMPPS_CHARGE_SLOW` (37), `NONXMPPS_CHARGE` (38) | `..._DUAL_BATTERY_AUTH_FAIL`, `_BATTERY_OCD`, `_BATTERY_CUV`, `_BATTERY_SCD`, `_XMPPS_CHARGER_SLOWLY`, `_NON_STANDARD_CHARGER` |
| | local defines `MCA_EVENT_QUICK_CHARGE_PMIC_TERM` (0x3d), `MCA_EVENT_FG_ERROR` (0x43), `MCA_EVENT_ONEWIRE_AUTH_PASS`; `_DEBUG_*` | `MCA_EVENT_MASTER_BATT_CLOSE`, `MCA_EVENT_BQ_FG_ERROR`, `MCA_EVENT_BATT_AUTH_PASS`, `MCA_EVENT_DEBUG_CTRL_*` |
| | literals `0x35`, `0x30..0x32`, `0x36` in `mievent_report` | `CHARGE_DFX_VBAT_MEAN_SHUTDOWN`, `CHARGE_DFX_ISP_LEVEL_1..3`, `CHARGE_DFX_BATTERY_OVERVOLTAGE` |

The three-way rotation in business (CP_VBUS_OVP / IBUS_OCP / VUSB_OVP) was a real naming trap: `case MCA_EVENT_CP_VBUS_OVP: report(CHARGE_DFX_CP_VBUS_OVP)` in the old source is, in stock terms, `case CP_VUSB_OVP: report(CP_VAC_OVP)` (the stock pairs: VUSB_OVP->VAC_OVP, VBUS_OVP->VBUS_OVP, IBUS_OCP->IBUS_OCP; the code matched by value, so behaviour was right).

### Other finished modules: names only, left unchanged (no rebuild; values equal stock)
| module (header tree) | spelling in source | stock name |
|---|---|---|
| charger_partition (portB) | `MCA_EVENT_DEBUG_DOUBLE85/_REMOVE_TEMP_LIMIT/_MEMORY_TEST/_SOC_LIMIT` (76..79) | `MCA_EVENT_DEBUG_CTRL_*` |
| mca_ibat_ocp_monitor (mcaA/mcaB) | `CHARGE_DFX_LOAD_SWITCH_IIC_ERR` (35) | `CHARGE_DFX_LOAD_SWITCH_I2C_ERR` |
| mca_bmd, mca_wireless_revchg, mca_event (local defines) | `MCA_EVENT_BMD_DELAY_REPORT` 0x50, `MCA_EVENT_WLS_REVCHG` 0x10 | `MCA_EVENT_BMD_STSTUS_CHANGE`, `MCA_EVENT_WIRELESS_USB_REVCHG` |
| mca_basic_wireless, mca_smart_charge, mca_quick_wireless | already on stubs_g13a (stock BTF values) | ok |
| mca_wireless_revchg | stubs_a17B says `REV_EN_BOOST=0`, `REVCHG_TYPE=0` | already fixed in the source (`MCA_REV_EN_BOOST=2`, `MCA_OVPGATE_REVCHG_TYPE=1`, stock values) |
| sc8581_sy6541, hl7603, bq27z561, nuvolta_1652, mca_connector_antiburn, mca_buckchg_jeita, mca_pwm_fan, mca_soc_limit, mca_vbat_ovp_monitor, mca_protocol_pd_class, mca_pd_auth, mca_qcom_*, mca_charger_thermal | `MCA_EVENT_TYPE_*` (notify types 0..10) and the `MCA_EVENT_*` ids they use: values equal stock; notify-type names equal the stock enum `mca_event_notify_type` except `MCA_EVENT_TYPE_CHARGE_STATUS` (stock `MCA_EVENT_CHARGE_STATUS`, value 5) | ok / alias |
| sc8581_sy6541 | audit tool flagged `MCA_EVENT_CP_*` because stubs_a17B is listed in its BUILD; the effective header tree is stubs_portB, whose values ARE the stock ones | ok |

Stock-vs-header disagreements in the shared headers that no finished module uses (so no effect): firmware_update_state (16), cp_int_stat_channel (8), cp_ovpgate_type (7), strategy_status_type (4: e.g. `STRATEGY_STATUS_TYPE_REV_TEST` 14 vs stock 7), cp_role `CP_ROLE_MAX`, and 100+ `charge_dfx_ele` values (full list: `enum_mismatch_by_tree.txt`). Any NEW module must take the stock BTF enum (stubs_g13d_* or stubs_g13a/g13b), never the old headers. A warning comment was added to line 2 of the old `mca_event.h` / `mca_charge_mievent.h` of 11 stubs trees (comment only).

## Caveats
- Modules without a local `.BTF` section were not audited by BTF (xr_qi, xr_qs have no such enums); the audit sees only names that exist in some stock BTF. Invented names with no stock counterpart cannot be judged by this method (the list `module_enum_usage_mismatch.txt` shows them as "stock_value_of_name=None").
- The proof "no byte change" is on the two modules that were rebuilt; the name-only aliases of the others were not touched.
