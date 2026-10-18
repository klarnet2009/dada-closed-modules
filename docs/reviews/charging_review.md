# Independent review: reverse-engineered charging / fuel-gauge modules vs HyperOS 4.0 (OS4) stock

Reviewer: independent agent (did not write this code). Date: 18.10.2026 (00:00-00:20 local).
Device: Xiaomi 15 "dada" (SM8750). Reference: `<work-dir>` (both copies identical; the `stock/` copies in every g2c..g6 have the same md5).
Nothing under `work/re/src` was edited, nothing was committed, flashed or built with Bazel. Server scratch used: `<work-dir>` only.

## 1. Summary

| module | claimed status | verdict |
|---|---|---|
| mca_strategy_fg_comp | PARTIAL (76/97) | **REAL DIFFERENCES FOUND** (at least 9 functions; one call passes an int where the OS4 API expects a pointer = kernel oops when the path runs) |
| mca_connector_antiburn | COMPILES-only (10/17) | **REAL DIFFERENCES FOUND** (monitor / probe / ensure_vbus_sense5V are template code, not the stock logic) |
| mca_wireless_revchg | PARTIAL (42/45) | **REAL DIFFERENCES FOUND** in `mca_wireless_update_fw_mainthread_work`; the other 3 functions: scheduling-only |
| mca_business_charger_comp | PARTIAL (41/45) | **REAL DIFFERENCE FOUND** in `business_charger_event_thread` (one missing call); the other 3 functions: scheduling-only |
| mca_pwm_fan | PARTIAL (16/20) | **SCHEDULING-ONLY DIFFERENCES** (all 4 functions), plus a kCFI type-hash mismatch on `pwm_reg_config` (no behaviour change) |
| mca_bmd, mca_protocol_pd_class, mca_charger_thermal, nuvolta_1652, mca_business_battery_comp, mca_platform_buckchg_class, mca_strategy_class, mca_event, sc8581_sy6541, mca_qcom_subpmic_proxy, mca_qcom_smem, mca_platform_cp_class, mca_platform_fg_ic_ops, mca_strategy_fg_class, mca_vbat_ovp_monitor, mca_ibat_ocp_monitor, mca_path_control, mca_soc_limit (18) | EQUIVALENT | **SAFE-TO-TRUST-AS-EQUIVALENT** at code/data level (caveats: section 4) |

The STATUS.md claims "only layout / register allocation" for `business_charger_event_thread`, `mca_wireless_update_fw_mainthread_work`, `mca_strategy_parallel_force_fw_report_full` and the other fg_comp functions are **wrong**: each of them has a real behavioural difference (evidence below).

Safety status of what is actually shipped: all 108 charging-related `.ko` in `<work-dir>` (vendor_dlkm + vendor_ramdisk: `mca_*`, `sc8581_sy6541`, `nuvolta_1652`, `bq27z561`, `xr_q*`, `charger_partition`, `fsa4480-i2c`, `qti_battery_debug`) are **byte-identical to the OS4 stock** (cmp). The standing rule is respected in the package. The ROM trees were not checked (forbidden directories).

## 2. Method (and what compare_ko.py does NOT check)

Snapshot of the sources taken at 23:54 (md5 list: `work/a17/review/charging_review_snapshot_md5.txt`; the four OVP/OCP/path/soc_limit modules were copied at ~00:10). Every snapshot was recompiled with the ThinLTO-emulation fast loop in `<work-dir>` (`bin/cl.sh`, copy of the g2i `cclm.sh`/`cc.sh` with my own paths) and compared with the newest agent `.ko`: masked word diff 0 for all 23 modules, i.e. the reviewed `.ko` files are exactly what the current sources produce (`__LINE__` immediates masked in that step; compare_ko on the `.ko` checks them).

Gaps found in `compare_ko.py` (`<build-dir>`), which the agents' "OK" lines rely on:
1. **Relocations are compared only for functions whose bytes are identical** (`bad_rel = [... if fa[n][0] == fb[n][0] and fa[n][1] != fb[n][1]]`). For every PARTIAL module the line "OK relocations" says nothing about the differing functions.
2. **Objects: only name + size of GLOBAL objects.** Data CONTENT (default tables, struct initialisers, thresholds in `.data`/`.rodata`) is never compared; static locals are not compared at all (7 static locals of the stock `fg_update_mapped_soc_new` are missing in fg_comp and nobody noticed).
3. **Anonymous rodata** (jump tables, `memcpy` sources of local array initialisers, switch lookup tables) is resolved as a C string, i.e. compared up to the first NUL byte: a table starting with 0 compares equal whatever follows.
4. `__bug_table` (BUG vs WARN flag), `__jump_table`, `.altinstructions`, `__ex_table` are not compared; kCFI type hash and `depends=` are not compared (known).
5. **Import CRCs do not validate prototypes** when the module is built against the forged `//re/stubs_a17B/os4:os4_*` providers: `work/a17/portB/gen_stubs.py` gives every export the stock CRC "without knowing any prototype", so the consumer's `__versions` CRC equals stock whatever prototype the consumer used. For byte-identical functions this is harmless (the ABI is in the bytes); for differing functions it hid a wrong prototype (fg_comp, finding F1).

Tools written for this review (copies in `work/a17/review/tools/`, live in `<work-dir>`):
- `semfp.py <stock> <built> [-g] [-s] [func..]`: per differing function, multiset diff of call targets, non-call relocation targets, immediates, struct-field accesses (mnemonic, width, offset), condition codes, and with `-g` the known argument values of every call (`callee(a0=..,a1=..)`: constants, strings, `ldr` field offsets, stack/field addresses). Equal multisets + equal call arguments + equal conditions => classified scheduling-only, then spot-read.
- `datacmp.py`: content of every named object (local + global) with relocations resolved, plus anonymous rodata referenced from code over its real extent (jump tables compared by case partition when the function bytes differ). Self-test: flipping one byte of `default_speed_adjust_cfg` in a copy of the pwm_fan `.ko` is reported; compare_ko reports nothing for the same change.
- `tblcmp.py`: `__bug_table`, `__jump_table`, `.altinstructions`, `__ex_table`, `.static_call_sites`, `__param`, ... entry by entry with resolved targets.
- `bin/fd.py` (copy of the g2i `fdiff.py` with the OS4 path), `bin/dis.sh`, plus `work/a17/kernel_pkg/preamble_cmp.py` (kCFI) and a `depends=` dump.

## 3. Findings per module (most severe first)

### 3.1 mca_strategy_fg_comp (fuel-gauge strategy, UI SoC, shutdown voltage) - REAL DIFFERENCES FOUND

Built object: `<work-dir>` (= snapshot). 21 functions differ. Static locals missing vs stock (datacmp): `fg_update_mapped_soc_new.{first_update_soc_flag,in_smooth_charging_flag,init_soc_smooth_flag,last_power_present,remove_charger_raw_soc,remove_charger_ui_soc,start_soc_smooth_time}`. kCFI 75/75 OK, tables OK. In monitor_workfunc the stock base register x19 = fg+8, offsets below are corrected.

F1 (HIGH if `support-qbg` is set, crash risk) `strategy_fg_monitor_workfunc` -> `platform_fg_ops_qbg_send_chg_data` has the wrong prototype.
 OS4 provider (stock `mca_platform_fg_ic_ops.ko`, and our EQUIVALENT rebuild `platform_fg_ic_ops_os4.h:155`) is `void platform_fg_ops_qbg_send_chg_data(unsigned int ic_role, void *chg_data)` and forwards the pointer to the QBG fuel-gauge op. Stock (monitor_workfunc +0x2078..0x21c4, x19 = fg+8): builds a struct at fg+0xd78: `charging = (status == 1)`, then ibat_max / iterm / vterm = `mca_strategy_func_get_status(1, 5 / 25 / 26)` while quick-charging, else the effective results of the fcc / iterm / vterm voters when `voter_ok`, else the defaults 500 / 565 / 4480 (`mov x9,#0x1f4; movk x9,#0x235,lsl#32; mov w10,#0x1180`), plus one more int; then `mca_strategy_func_get_status(7, 19 [JEITA_HIGH_TEMP_RANGE], &tmp)` and `if (!tmp) platform_fg_ops_qbg_send_chg_data(0, &fg->qbg_data)` (`add x1, x19, #0xd70`).
 Ours (`mca_strategy_fg.c:3475-3486`, header `stubs_g2i/.../platform_fg_ic_ops.h:151` `(unsigned int, int fcc, int final_fcc)`): `ldr w1,[x19,#0xaec]; mov w2,wzr; bl platform_fg_ops_qbg_send_chg_data` - an integer reaches a provider that expects a pointer (a QBG op that dereferences it would oops), and the content (chg_status / final fcc) is not what the stock sends. Ours gates it with `cfg.support_qbg`; "support-qbg" was not found in the OS4 dtbo.img (string search), so it may be inactive on dada. compare_ko could not see it (forged import CRC, gap 5). Fix: OS4 prototype + struct fill + JEITA gate as in the stock listing.
F2 (HIGH, fuel gauge / UI) `fg_ui_soc_smooth`: stock 802 insns / 32 calls, ours 485 / 16. The whole OS4 "new SoC smooth" (`fg_update_mapped_soc_new`, logs `smooth_new:: ...` lines 189-270, `ktime_get_coarse_with_offset`, `mca_get_effective_result`) and the OS4 special conditions (`WARNING: not allow ui_soc jump to 0` line 283, `not allow ui_soc increase when resume` 286, `allow ui_soc jump, boot_update_soc` 290) are missing; `mca_soc_smooth.c` is the older revision. `support_new_soc_smooth` occurs in the OS4 `dtbo.img` (string search), so the reported battery percentage would differ on dada.
F3 (HIGH, low-voltage shutdown) `mca_battery_shutdown_parse_dt`: the stock reads `of_find_property(node, "support-dod-vcutoff-v2")` -> `strb [x19,#0xc01]` (= `support_dod_vcutoff_v2`), sets `batt_spec_para.total_gear` (`str w8,[x19,#0xc24]`) and `init_update_siloss = 1` (`str w9,[x19,#0xc30]`) and fails with `temp_para_size is 0, cannot access temp_info[0]` (line 333, kfree, return -1). Our source never sets `support_dod_vcutoff_v2` (grep: no "support-dod-vcutoff-v2"), so the whole v2 vcutoff strategy (`strategy_update_vcoutoff_v2_work`) is dead; `total_gear` stays 0 (would give index -1 in `min(..., total_gear - 1)` if v2 were ever enabled).
F4 (HIGH, low-voltage shutdown) `mca_battery_shutdown_update_vcutoff_para`: stock `mutex_lock(&fg->temp_index_lock)` (+0x58) and `mca_battery_shutdown_update_cc_para(fg, &fw, &delay, &sw)` (+0x6c), return value ignored, then the DOD part. Ours computes the CC part inline (OS3 code, log `BatteryHealth:cur_temp_index...` line 784 which the stock does not have), no lock, and returns -1 from the whole update when the temperature is outside every CC range, so at extreme temperatures the DOD cut-off values are not refreshed while the stock refreshes them. `support-cc-vcutoff` and `support-dod-vcutoff` occur in the OS4 dtbo, so this path is live on dada.
F5 (MEDIUM, charge termination) `strategy_fg_monitor_workfunc` (inlined `mca_strategy_parallel_first_termination`): stock +0x3de8..0x3e48: `if (base_flip_same && !fast_charge && fc_master && fc_slave) { if (last_target_iterm != iterm + 40) { last_target_iterm = iterm + 40; mca_vote_override(fg->iterm_voter, "para_iterm", true, iterm + 40); } log "iterm is %d" (1396) }` plus the matching `mca_vote_override(..., "para_iterm", false, 0)` and 4 `mca_get_effective_result` calls: missing in ours.
F6 (MEDIUM) `strategy_fg_monitor_workfunc` (`charge_get_batt_rm_threshold`): stock `mca_strategy_func_get_status(1, 23, &device_power_max)`; ours `(1, 20, ...)` (`#define STRATEGY_STATUS_TYPE_DEVICE_POWER_MAX 0x14` at `mca_strategy_fg.c:2797`; OS4 BTF enum: 20 = CP_TO_PMIC, 23 = DEVICE_POWER_MAX). The RM threshold (67000 / 100000 mW steps) is computed from the wrong status value. Also missing in ours: `mca_strategy_func_get_status(7, 17 [JEITA_COLD_ZONE], ...)` and `(7, 19 [JEITA_HIGH_TEMP_RANGE], ...)`, one `memset(event,0,500)`; ours has an extra log `boot_update_soc update is_eu_model done` (2844) and static `last_rm`. Log lines 2883/2114/1927/1955 vs stock 2882/2714/1926/1954.
F7 (MEDIUM, charge-full handling) `strategy_fg_ops_set_charging_done`: stock 225 insns / 23 calls, ours 116 / 11. Missing: `After charging done, if RSOC is not 100, set force full cmd` (lines 4024/4036: `platform_fg_ops_set_force_report_full(1)` / `(0)` x2), `charging_done start isc_detect_work` (4029, `queue_delayed_work_on(.., fg+0x5e0, 300000)`), li-plating `full_count` / `charge time too low, stop work` (4041/4047, `stop_li_plating_detect_work`), `cancel_delayed_work_sync(fg+0x6f0)`, `mca_vote_override(fg->iterm_voter, "para_iterm", false, 0)`.
F8 (MEDIUM) `mca_strategy_parallel_force_fw_report_full`: (a) stock +0x6dac..0x6dc0 `if (base_flip_same) count = cold_zone ? 3 : count` (`ldr w11,[x20,#0xd3c]` = `cold_zone`; `csel w24, w24, w11(3), eq`), missing in ours; (b) base_flip_same && !fast: stock `force_iterm = iterm + 30; force_vterm = vterm - 23` (+0x7024 `add w23,w21,#0x1e; sub w22,w4,#0x17`), ours `vterm - 15` for both fast and non-fast (`mca_strategy_fg.c` ~2350). Thresholds of the "force report full" decision differ.
F9 (MEDIUM, config) `strategy_fg_probe`: stock parses `support_clear_cycle` (`of_find_property`) - ours never does; the stock `strategy_fg_parse_dt_override` re-parses only a subset with its own log strings ("override parse ... failed", lines 3863-3915) and keeps the current values as defaults (`capacity_comp` default = current value), ours re-runs the whole `strategy_fg_parse_dt_node` (drop_fv_cycle / support_new_soc_smooth parsed twice) - relevant only for hwid "donghai"/"yanan"; `strategy_full_design_parse_dt` is inlined differently (same strings, default 5000).
F10 (LOW) `mca_strategy_get_manufacturing_date`: different log text/line (stock `get parallel_manufacturing_date: ...` 2151, ours `... fail: ...` 736); the master/slave selection code differs in shape and was NOT verified. `mca_strategy_get_first_usage_date`: shape differs, NOT verified.
F11 (LOW, unreachable) array-bounds trap constants differ: `isc_do_point_sample_2` index check `< 5` (stock) vs `< 6` (ours), `strategy_li_plating_work` `< 10` vs `< 11` - the indices are `% 5` / `% 10`, so the traps are unreachable, but our array declarations do not match the stock; `isc_do_point_sample_2` log lines 243/253 vs 244/254 and the `time` argument is loaded 64-bit in the stock, 32-bit in ours (printed with %d: same output); `mca_strategy_check_li_plating` log line 379 vs 380.
Scheduling-only (verified): `strategy_batt_overvoltage_dfx_work` (stock `v > 4199` vs ours `!(v < 4200)`, `x < 1` vs `x <= 0`), `strategy_isc_detect_work` (signed vs unsigned `/10`, `%5` after a `>= 1` check: same results), `report_isc_data_2` (64-bit vs 32-bit time subtraction, same low 32 bits). Fingerprint-identical (calls+args, immediates, fields, conditions; not read line by line): `strategy_fg_process_event`, `strategy_fg_panel_notifier_cb`, `fg_update_status`, `report_isc_data`. Not analysed: `strategy_isc_detect_2_work`, `strategy_update_vcoutoff_v2_work` (dead in ours, see F3), rest of `strategy_li_plating_work`; monitor_workfunc (2033 vs 1926 insns) and probe were not decoded completely - more differences are likely.

### 3.2 mca_connector_antiburn (USB connector over-temperature protection) - REAL DIFFERENCES FOUND

Only a fast-loop object exists (no Kleaf build). 7 functions differ; datacmp: global `flag_restore` missing in ours.
- `connector_antiburn_monitor_workfunc`: stock 973 insns / 132 calls, ours 421 / 50. Missing: 3 calls of `ensure_vbus_sense5V`, uevents `POWER_SUPPLY_CONNECTOR_TEMP`, `POWER_SUPPLY_NTC_ALARM`, `POWER_SUPPLY_ADAPTER_RESET_VSAFE0V` (stock 14 `mca_event_report_uevent`, ours 4), mievent `mca_charge_mievent_report(10, ..)` x4 / `(24, ..)` x2 / `set_state(1,10)`, `mca_event_block_notify(4, 38, 0)` (stock 6, ours 2), boost / cid / otg / `protocol_class_pd_request_vdm_cmd` handling, temp-increase-rate logic (statics `last_connector_temp`, `not_attched_count`), all "triggering/recovery antiburn" logs. `mos-ctrl-gpio` and `support_elaboration_anti_strategy` occur in the OS4 dtbo, so the v2 path is live on dada.
- `connector_antiburn_ensure_vbus_sense5V`: completely different. Stock: `protocol_class_get_adapter_type` x5, `platform_class_buckchg_ops_get_bus_volt` x6, `protocol_class_pd_request_vdm_cmd(0, 255, ..)` x2, `msleep(200)` x4, log `real_type is %d, bus_volt is %d` (226). Ours (template): `protocol_class_qc_set_volt(0, 5000)`, `protocol_class_set_adapter_volt_and_curr(0, 5000, 1500)`, `platform_class_cp_enable_adc`, `platform_class_cp_get_bus_voltage` - it actively commands the adapter voltage/current with values the stock never uses.
- `connector_antiburn_probe` (563 vs 451 insns), `antiburn_sysfs_store` (stock also `cancel_delayed_work_sync` + `mca_queue_delayed_work(.., 250)` after a write, ours not), `connector_antiburn_get_temperature` (stock sets the global `flag_restore`, ours not). `is_triggered`, `antiburn_sysfs_show`: fingerprint-identical.
Fix: rewrite monitor (incl. check_status v1+v2, get_temp_increase_rate), ensure_vbus_sense5V and probe from the OS4 disassembly; until then this source must be treated as unverified.

### 3.3 mca_wireless_revchg (wireless reverse charging, pen charging, wireless FW update) - REAL DIFFERENCES FOUND

Built: `<work-dir>` (= snapshot). kCFI 42/42 OK, data OK, tables OK.
W1 (MEDIUM) `mca_wireless_update_fw_mainthread_work` - missing logic at the end, stock +0x3658..0x3698:
```
pm_relax(info->dev); wls_sleep_fw_update = 0 (strb +0x467); fw_updating = 0 (strb +0x460)
mca_event_block_notify(0, 4, &info->proc_data.fw_updating)        <- missing in ours
if (!wls_sleep_usb_insert) platform_class_wireless_set_enable_mode(0, 1)
if (info->support_tx_only) mca_wireless_rev_enable_reverse_charge(true)   <- missing (ldr w8,[x19,#0x44c] = info+0x8b4 = support_tx_only)
```
 i.e. other MCA modules are not told that the FW update ended, and a TX-only (pen) device does not restart reverse charging after a FW update.
W2 (MEDIUM) same function (inlined `mca_wireless_rev_download_firmware`): the state reported before `platform_class_wireless_download_fw` is `firmware_update_state = 2` + uevent `POWER_SUPPLY_WLS_FW_STATE=2` in the stock (+0x3710 `mov w8,#0x2`, `mov w3,#0x2`), `1` in ours (`FIRMWARE_UPDATING` from the stubs enum). OS4 BTF: `enum fw_update_status { NO_UPDATE=0, NEED_UPDATE=1, UPDATING=2, FINISH=3, ERROR=4 }`. Fix: `#define MCA_FW_STATE_UPDATING 2` next to the existing FINISH/ERROR defines.
Scheduling-only (verified with call arguments, immediates, fields, conditions): `mca_wireless_rev_charge_config` (same 126 calls, same arguments, same constants; only the order of tail-merged `__mca_log_err` calls and two duplicated `__func__` adrp), `mca_wireless_rev_charge_config_work`, `mca_wireless_rev_enable_reverse_charge` (fingerprint-identical).

### 3.4 mca_business_charger_comp (charger business logic: events, USB psy, quick-charge type) - REAL DIFFERENCE FOUND

Built: `<work-dir>` (= snapshot; same md5 as the g2h copy). kCFI 37/37 OK, data OK, tables OK (`.altinstructions` differ only in where the ldapr replacement lives; replacement bytes identical).
B1 (LOW-MEDIUM) `business_charger_event_thread`, inlined `business_charger_process_batt_health_change` (event 0x13): stock +0x21c4..0x2200 and +0x2a50:
```
if (real_type == 11 || real_type == 12) { cancel_delayed_work_sync(&report_quick_charge_type_work); queue_delayed_work(system_wq, .., 0); }
else business_charger_update_wired_quick_charge_type(charger);      <- missing in ours (mca_business_charger_comp.c:1048)
mca_strategy_func_process(2, 0x13, 0);
```
 `update_wired_quick_charge_type` re-reads the battery HEALTH (sets `abnormal_info.batt_cold/batt_overhot`) and re-reports the quick-charge icon type; ours skips that for every non-PPS/PD charger on a battery-health event. STATUS.md says "no longer calls update_wired_quick_charge_type": wrong. Fix: add the `else` branch. semfp shows this is the only call-level difference in the 840-instruction function (call arguments, immediates, fields, conditions otherwise equal; jump table: same case partition).
Scheduling-only (verified): `business_charger_report_quick_charge_type_work` (stock duplicates the `>= 50` ladder; ours tail-merges; stores identical), `usb_psy_get_prop` (stack-slot colouring only), `business_charger_probe` (ours reloads `pdev->dev.of_node` instead of keeping it in a register).

### 3.5 mca_pwm_fan (cooling fan accessory) - SCHEDULING-ONLY DIFFERENCES

Built: `<work-dir>` (= snapshot). Data / tables OK.
- `set_pwm`: only a different tail-merge of the `pwm_fan_power_on failed` log (line 207) and `return ret` vs `return 0` on success paths, where `ret` comes from `pwm_reg_config` = `pwm_apply_state()` (0 on success): equivalent.
- `set_pwn_fan_target_level`: `(level-101) >= 4` vs `> 3` (same), `(enabled != 0) ^ (level == 0)` + `tbnz` vs `(level != 0) == (enabled != 0)` + `b.eq` (same truth table, checked for all 4 cases). `fan_sysfs_store`: the same XOR spelling. `speed_detect_req_work_func`: the stock zeroes 8 bytes more of `event_data`, but both fields are written before use.
- P1 (LOW, no behaviour change) kCFI preamble mismatch: `pwm_reg_config` stock hash 0x3c05da7f = `int (struct pwm_fan_ctx *, int)`, ours 0x50818d01 = `int (struct pwm_fan_ctx *, unsigned int)` (hashes computed with clang-r510928 on test prototypes). Bytes identical, the function is not exported and not called indirectly, so no runtime effect; fix the parameter type to `int duty_percent`.

### 3.6 The 18 EQUIVALENT modules - SAFE-TO-TRUST-AS-EQUIVALENT

Checked: compare_ko (all OK, EQUIVALENT), datacmp (named objects incl. statics: content identical; anonymous rodata identical), kCFI preambles 0 mismatches, `__bug_table`/`__jump_table`/`.altinstructions`/`__ex_table`/`__param` identical (subpmic_proxy: altinstr only relocated location differs, replacement bytes identical), `.gnu.linkonce.this_module` init/exit identical, snapshot rebuild == agent `.ko`.
.ko used: g2e (bmd, protocol_pd_class, charger_thermal, nuvolta_1652, sc8581_sy6541), g5 (business_battery_comp, qcom_subpmic_proxy, qcom_smem), g6 (platform_buckchg_class, strategy_class, event, platform_cp_class, platform_fg_ic_ops, strategy_fg_class), a17B (vbat_ovp_monitor, soc_limit), a17A (ibat_ocp_monitor, path_control).
Hacks reviewed: `#line` pins, `noinline`, lock-key `section(".bss...__key")` (nuvolta also puts `static u16 total_length` there: still a separate 2-byte .bss variable), sc8581 GENKSYMS-only `__attribute__((fcrc...))` export-CRC forging (the compiler never sees it; export CRCs equal the stock and the code is the stock code, so the ABI promise is true). None can change run-time behaviour: code, data and tables are identical to the stock.
Caveats (not code differences):
- `depends=` differs by provider names in 7 modules: `os4_*` (protocol_pd_class, charger_thermal, nuvolta_1652 partly, business_battery_comp partly, sc8581_sy6541) and `mca_st_*` (bmd: `mca_st_mca_charge_mievent`; platform_cp_class: `mca_st_mca_log`, `mca_st_mca_parse_dts`). These `.ko` are not drop-in: they must be relinked against the real providers before any use.
- vermagic is 6.6.144 (ours) vs 6.6.57 (stock): expected.
- `mca_qcom_smem`: `.note.Linux` LTO_INFO = 0 (no ThinLTO defconfig) vs 1 in the stock; metadata only.

## 4. Safety notes (what each module controls)

The standing rule (charging modules stay stock in the kernel package and in any ROM) must hold for ALL of them, EQUIVALENT included, until hardware verification: EQUIVALENT proves equality of the binary code, not of the build/link/load context (providers, depends, kernel ABI).
| module | controls | flash risk |
|---|---|---|
| mca_strategy_fg_comp | UI SoC / battery %, force-full, termination iterm votes, JEITA-gated QBG data, low-voltage shutdown cut-off (CC/DOD/v2), li-plating / ISC detection | NEVER (crash risk F1, wrong shutdown voltage logic F3/F4) |
| mca_connector_antiburn | USB connector overheat: MOS cut-off GPIO, VBUS drop to 5 V / 0 V, adapter commands | NEVER (wrong protection logic, commands adapter voltages) |
| mca_wireless_revchg | reverse wireless charging (boost / hboost voltage ramps), pen charging, wireless chip FW update | NEVER |
| mca_business_charger_comp | event hub of the charger, USB psy props (VOLTAGE_MAX/CURRENT_MAX), quick-charge type, input suspend vote | NEVER |
| mca_pwm_fan | cooling fan PWM / LDO | not charging, but stays stock (P1) |
| mca_vbat_ovp_monitor / mca_ibat_ocp_monitor | battery over-voltage / over-current protection | stock only |
| mca_charger_thermal, mca_strategy_class, mca_strategy_fg_class, mca_soc_limit, mca_path_control, mca_bmd | thermal current limits, strategy dispatch, FG class, SoC limit, charge path, battery-missing detection | stock only |
| sc8581_sy6541 | charge pump (CP) IC driver: CP OVP/OCP thresholds, enable | stock only |
| nuvolta_1652 | wireless charger IC driver + firmware blobs | stock only |
| mca_platform_* / mca_protocol_* / mca_qcom_* / mca_event / mca_business_battery_comp | buck charger / CP / FG IC ops, PD/QC protocol, PMIC glink proxy, events, battery psy | stock only |

## 5. Not checked / limits

- No hardware, no Kleaf build by me (agent `.ko` + my fast-loop reproduction of the same bytes). DT evidence is a string search in `<work-dir>` (the base dtb of vendor_boot is not extracted): indicative, not a parsed DT.
- The semfp classification is a fingerprint (multisets + known call arguments + condition codes) plus spot reading; it does not prove data flow through registers. "Fingerprint-identical" functions were not all read line by line.
- fg_comp: `strategy_fg_monitor_workfunc` and `strategy_fg_probe` were not decoded completely (more differences likely); `isc_detect_2_work`, `update_vcoutoff_v2_work`, most of `li_plating_work`, the selection logic of `get_manufacturing_date` / `get_first_usage_date` not verified.
- antiburn: only fingerprinted, not decoded.
- Not in scope / not checked: xr_qi, xr_qs, bq27z561, mca_buckchg_jeita, mca_basic_wireless, mca_quick_wireless, mca_smart_charge, mca_strategy_buckchg, mca_strategy_quickchg (all BLOCKED/PARTIAL per STATUS), and EQUIVALENT modules not listed in 3.6 (e.g. mca_platform_wireless_class, mca_pd_auth, mca_charge_mievent, mca_common, mca_lpd_detect).
- ROM build trees (`<work-dir>`, `/out`, `/a17`) not inspected (forbidden).
