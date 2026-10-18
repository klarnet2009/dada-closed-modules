# Charging family: audit of "block order only" functions (goal-G13o, 19.10.2026)

Status: community-only sources; never flash, never in a ROM build or the kernel package; NOT hardware-tested; this audit makes NO battery-safety claim. Everything below comes from the stock OS4 disassembly and symbolic comparison; no phone, no git.

## 1. What was done

- Every charging module that has a non-byte-identical function was rebuilt from the CURRENT sources with real Kleaf (`work/re/g13o_tools/kb.sh`, server `<work-dir>`, one Bazel build at a time, bazel-out deleted after each compare) and compared with the OS4 stock `.ko` (`compare_ko_v2.py -v`, `preamble_cmp.py`, then `symcmp` on EVERY differing function, not only on the ones analysed earlier).
- `symcmp` was extended into `work/a17/review/tools4/symcmp4.py` (the original `tools3/symcmp.py` is unchanged): value-exact canonicalisation of sign/zero-extension forms (`sxtw(ldr.w)` = `ldrsw`, `and 0xffffffff(ldrsw)` = `ldr.w`), `tbz #0` = `cbz` on a `bool` loaded by `ldrb`, `ldrsw` in a 32-bit compare = `ldr.w`, "mutually exclusive switch case" guards (`NOT x==2` is implied by `x==1`), and an effect-oriented summary per function: effects (call / store / stack-data / atomic / trap) only-stock / only-ours / count / guard differences, return-value differences, pure compare-form differences, a phi-insensitive second opinion (disable with `SKIPPHI=1`, needed for the huge fg_comp functions) and a comparison of stack-passed data with call-result labels normalised.
- Classification rule used:
  - class (a) = scheduling / registers / block order / loop-counter direction / compare form / tail merging / stack layout, proven by: all effect facts (call arguments, memory stores, stack data) equal AND the must-guards of every effect equal (so branch polarity is equal); remaining differences are only compare forms whose constants I re-checked in the listing, log `__LINE__` values, or `out(...)` labels of stack slots.
  - class (b) = hidden behaviour: a missing / extra call, store, constant, string, fallthrough, polarity, signedness/width, bound.
- Not rebuilt (STATUS says all functions byte-identical; re-verified by a fresh real build + compare): mca_bmd 0 differing, charger_partition 0 differing, mca_ibat_ocp_monitor 0 differing (only buildmeta/depends fail, as everywhere). The remaining modules of the family are "EQUIVALENT" in their STATUS (no differing function) and were not rebuilt.

## 2. Results per module (current sources, after the fixes of section 4)

| module | functions | byte-identical | differing, checked | class (a) | class (b) | fixed here |
|---|---|---|---|---|---|---|
| mca_smart_charge | 35 | 29 | 6 | 6 | 0 | 0 |
| mca_connector_antiburn | 17 | 12 | 5 | 5 | 0 | 0 |
| mca_buckchg_jeita | 17 | 13 | 4 | 4 | 0 | 0 |
| mca_wireless_revchg | 45 | 43 | 2 | 2 | 0 | 0 |
| mca_business_charger_comp | 45 | 41 | 4 | 4 | 0 | 0 |
| mca_pwm_fan | 20 | 16 | 4 | 4 | 0 | 0 |
| mca_strategy_fg_comp | 97 | 79 | 18 | 18 (see caveats) | 0 | 0 |
| mca_strategy_buckchg | 61 | 48 (was 45) | 13 (was 16) | 13 | 3 found | 3 |
| mca_basic_wireless | 73 | 63 | 10 | 9 | process_irq (known, unfixed) + tx_q kmalloc (fixed) | 1 |
| mca_quick_wireless | 37 | 29 (was 28) | 8 (was 9) | 5 | 4 fixed + 3 known-unported open | 4 edits |
| mca_strategy_quickchg (G13n owns it: NOT edited, snapshot of 19.10 ~00:15) | 83 | 39 (bytes and relocations; 42 by bytes only) | 44 | 4 | many, see 3.9 | 0 |
| bq27z561 | 207 stock / 163 built | 67 | 95 | 20 | 75 not equal, 44 stock functions missing | 0 |

After the fixes: kCFI preambles PASS and preamble_cmp 0 differences in buckchg 45/45, basic 58/58, quick 18/18; function sets unchanged. Nothing is STRICT-EQUIVALENT (buildmeta / depends / forged providers).

## 3. Per module

### 3.1 mca_smart_charge (6 differing, all class (a))
- `smart_charge_sysfs_store`, `smartcharging_handle_algorithm_conflict`, `smart_charge_handle_sense_chg`, `smart_charge_workfunc`: effects, guards equal; `workfunc` only commutes the equal tests 700/702 (mutually exclusive); `handle_sense_chg` ccmp: stock `x==3 || x>=6`, ours `NOT(x!=3 && x<6)` (constants 3 and 6 checked in the listing).
- `smart_charge_check_bypass_status` (G13h fix of the early return stays correct): the static `once_flag` is a `bool` in both; stock keeps the loaded value in a callee-saved register across the log call, ours reloads it; `tbz #0` vs `cbz`. Same effects, same polarity.
- `update_smart_bypass_temp_section`: `ldr.w` vs `ldrsw` on the same index, and log phi forms only.

### 3.2 mca_connector_antiburn (5, all (a))
`antiburn_sysfs_show`, `connector_antiburn_probe`: equal. `antiburn_sysfs_store`: the 4th argument of `gpiod_direction_output_raw` is the value kstrtoint wrote into a stack slot (label difference only). `connector_antiburn_get_temperature`: stock stores the constant 1 into `flag_restore`, ours the value of `adc_err_flag`, under the guard `adc_err_flag == 1` (constant folded: same). `monitor_workfunc`: `flag_restore` (bool, written only with 0/1) tested as `!=0` vs `==1`.

### 3.3 mca_buckchg_jeita (4, all (a))
`monitor_work`: `base_over_curr_count` increment/compare form (stock: store of `ite(cond, n+1, 0)` then `n' < 4`; ours: `n+1 < 4`; the redundant `effective_curr != 0` is implied in stock); `probe`: the second inlined copy of `u32 weak_cfg[2]` is not zero-initialised in ours (stock `= {0}`), only read after a successful parse; `get_status`: jump-table phi noise; `parse_dynamic_iterm_by_name`: stack layout.

### 3.4 mca_wireless_revchg (2, both identical by symcmp), mca_business_charger_comp (4), mca_pwm_fan (4): all (a)
- charger_comp: `report_quick_charge_type_work` (stock two stores of `wired_power_max` by path, ours one merged store of the same `max()`/value, compare `< 50` on the same value), `event_thread` (log phi), `usb_psy_get_prop` (stack labels), `probe` equal.
- pwm_fan: `set_pwm` (stock has a separate error-log site for line 207, ours merges the same error text with a phi over the line numbers 174/184/190/196/207/213), `set_pwn_fan_target_level` / `fan_sysfs_store` (xor form of `(old!=0) == (new==0)`, same truth table), `speed_detect_req_work_func` equal.

### 3.5 mca_strategy_fg_comp (18, all (a) with caveats)
- Effects equal for all; the remaining facts are stack-passed buffers whose slots move (`get_error_state`, `mievent_report`), log `__LINE__` merged by tail merging (`mca_battery_shutdown_parse_dt`: lines 172/200/215/275/289 all present), compile-inference forms: signed vs unsigned division by 5/10 on sample indexes (`isc_detect_work`, `isc_detect_2_work`, `vcoutoff_v2_work`, `li_plating_work`, `isc_do_point_sample_2`): ours lets clang infer a non-negative index from an earlier `idx > 0` test, stock computes signed; identical for every non-negative index (these indexes are only incremented from 0).
- `strategy_li_plating_work` / `report_li_plating_data`: the bound guard `before_full_index < 10` (stock) vs `< 11` (ours) is the UBSAN `-fsanitize=array-bounds` trap guard: ours takes `&li_data_1[idx]` (one past the end allowed), stock reads `li_data_1[idx].x`. The index is `sample_index % 10`, so 10 never occurs. Not a behaviour change.
- `strategy_fg_monitor_workfunc`: `fg_update_status(fg, !boot_update_soc)` (ours) vs constant 1 on the path after `!resume_update_soc` (stock): same value; the `+3440` (`chg_data.quick_chg_en`) store is `== 1` of the same stack slot; `ibat_check` flag `+3537` store tail-merged once instead of twice.
- Caveat: `fg_update_status` (1169 insns) and `monitor_workfunc` (2032 insns) have 100+ stack-data facts that move; I compared with call-result labels normalised and found only offset changes of output buffers; not proven line by line.

### 3.6 mca_strategy_buckchg (13 differing after the fixes, all (a); 3 class (b) found and fixed)
Class (b), fixed (now byte-identical to stock):
1. `strategy_buckchg_process_batt_btb_change` (`mca_strategy_buckchg.c:845-858`, stock `.text+0x5d2c`): stock releases `mca_vote_override(input_limit_voter, "master_batt_missing", false, 0)` ONLY in the `value == 0` (battery back) branch (+0x5dd4 is inside that branch, the `value != 0` path jumps to the epilogue at +0x5d9c); ours released it unconditionally, i.e. also on the "battery missing" event, removing the master-battery-missing input-current limit exactly when the battery is missing.
2. `strategy_buckchg_if_set_chg_en` (`:2912-2935`, stock +0x6db8): stock clears the `pmic_temp_term` overrides on vterm and ICL only when charging is being ENABLED (`value != 0`); ours cleared them also on a disable request (the sysfs/debug `charge_enable = 0` path).
3. `strategy_buckchg_if_set_input_suspend` (`:2974-3000`, stock +0x6c2c): the same, only when the suspend is RELEASED (`temp_value == 0`).
Class (a) functions (13): `class_probe` (stkarg layout, `mutex_init` lock keys, `term_curr` votable type is `ite` of the same condition), `monitor_workfunc` (effects 538/538 equal; guard differences are the commuted pair `soc <= 94` / `info+1512 == 0` of the "first FFC branch only logs the cycle" test: pure reads, same result), `base_flip_sw_cv_workfunc`, `wls_revchg_monitor_workfunc` (retry counter `cnt<5` vs `cnt+1==6` end at the same iteration; range test `lo<=v<=hi` ccmp vs two branches), `source_status_monitor_workfunc` (stock loop counter 5 down with `subs`, ours -5 up with `adds`: 5 iterations each; the `ibat_check_cnt` store uses the `screen_status` sampled before `get_current` in stock, after it in ours: only a race), `process_event` / `get_status` (jump-table phi noise only; effects equal), `set_vterm` (compensation 45/40/30/0 and the 33/34 C tests equal), `set_charge_volt`, `process_antiburn_change` (the two inlined "pwr_cap nums is null" logs have the `__LINE__` pins 2027/2035 swapped relative to the stock inlined copy: log line only; NOT changed because the byte-identical standalone `set_charge_volt` needs the current pins), `process_csd_pulse`, `cp_revert_handler` (switch cases tested in the other order), `is_pdo_9v`.

### 3.7 mca_basic_wireless (10 differing)
- (a): `get_adapter_charger_mode`, `split_irq_index`, `base_flip_sw_cv_workfunc`, `adapter_handle`, `sysfs_store` (equal); `monitor_work` (loop counter 15 down vs -15 up); `max_power_control_work` (stock logs from two sites with lines 3582/3587 which the compiler merges into one call with a phi; ours one site, line 3587: log line only); `process_event` (phi noise only, effects equal); `class_probe` (after the fix below only a stack layout fact and the wait-queue lock keys).
- (b) fixed: `tx_q1` / `tx_q2` buffers in `strategy_wireless_parse_q_vaule` (`mca_basic_wireless.c:4155,4174`, stock probe `.text` `__kmalloc(.., 0xdc0)`): stock zeroes them (kzalloc), ours used kmalloc/kmalloc_array. Effect: none visible (read only after a successful parse), the facts of `class_probe` now match 456/456 except two.
- (b) NOT fixed, known and listed in STATUS: `strategy_wireless_process_irq` (stock `.text+0x5f00`, only-stock 7 / only-ours 39 effect facts): the mievent report is ONE call site with a computed type (stock `phi{26|27|28|29}`, value `phi{0|1|2}`) and ours has two sites (26 with 2; 27/28/29 with 0/1) so the (type, value) mapping is not proven equal; the TX voltage range 20000/32000 (stock `phi`, ours `ite(uuid == 0x09010a01, 20000, 32000)`: the `platform_version == 3` gate is elsewhere; on dada `platform_version` is 2 per G13m, so 32000 mV is used); the log line number of RX_INT_TRANSPARENT_SUCCESS (2534). No effect of the 38 only-ours facts could be traced to behaviour: they are stack-data facts of the inlined `chg_handler`.

### 3.8 mca_quick_wireless (8 differing after the fixes)
(b) found and fixed (`mca_quick_wireless.c`):
1. `can_tbat_do_charge` (stock .text+0x3b24, ours `:1314`): when the temperature zone changes, the stock sets `proc_data.zone_changed` (info+920, `bool`, named zone_changed in the BTF header, previously never written or read); ours never used it.
2. `select_stage` (stock +0x3558..0x356c, ours `:1163`): stock re-selects the stage when `stage > cur_stage || zone_changed || ffc_flag != ffc_flag_now`, then clears `zone_changed`; ours only `stage > cur_stage || ffc differs`, so after a temperature-zone change the stage stayed on its old (later, lower-current) value: safe-side but not stock.
3. `select_stage` second loop (base flip only, inactive on dada): stock tests `stage != parall_cur_stage` (can go down) and does NOT call `get_parallel_volt_para` there; ours used `>` and called it (`:1176,1201`). Now as stock.
4. `get_parallel_volt_para` (`:539`): the "no volt para" log is `mca_log_err` in stock, was `mca_log_info` (log level). Now byte-identical.
After these: `can_tbat_do_charge` and `get_parallel_volt_para` are byte-identical (the previous STATUS claim "differ only in the real build" was wrong: they had real differences), `select_stage` facts differ only in phi labels.
(a): `monitor_work` (CP-ok flag `cbz` vs `tbnz`, 249/249 facts), `stop_charging` (stock `value <= 400` clamp as `lt 401`, ours folded `product < 4400000` = `< 400`: same result 400), `change_bridge` (15-iteration loop counter direction), `select_cur_work_mode` (min/max forms: the index select `(a<b)` vs `(b>a)`).
(b) NOT fixed, known:
- `process_event` (stock `.text+0x1670`): the stock accepts events 2..73, ours 2..68: missing `FG_OTA_PROCESS` (73) = `mca_vote(chg_disable_voter, "fg_ota_process", true, value)` (stock +0x430, "fg_ota_process event %d" log) and `CP_NEW_MODE` (37), the soc stop ("soc = %d, stop quick charge", +0x3f8), the `taper_done_no_retry = false` store (+0xa8), `st +984 = 1` (+0x324), and the `base_flip_cc` log. Fuel-gauge OTA does not stop wireless quick charging in our code.
- `parse_batt_info` (stock `+0x1dc8`): 93 stock-only facts (the whole battery-parameter parse with `curr_batt_cycle` is incomplete).
- `select_max_ibat` (stock +0x3d7c): the base-flip branch uses `cur_stage` (+840) instead of `parall_cur_stage` (+848) for the `cur_base_stage` ocp bound, lacks `set_flip_cur` (static `last_flip_curr_max`, `platform_class_loadsw_set_ibat_limit`) and calls `get_parallel_volt_para` twice. Inactive on dada (`support-base-flip` is absent in the OS4 DT).

### 3.9 mca_strategy_quickchg (REPORT ONLY; G13n owns the edits; snapshot built ~00:50 from the then current sources)
44 differing functions: 3 auto-class (a) (`msleep`, `get_cpmode_unrelated_fcc`, `cp_check_initial_mode`), 1 compare-form-only (`select_cp_work_mode_by_current`), 40 with facts that differ. Besides the known unported bodies (`monitor_work` 132 stock-only effects, `regulation` 210, `process_event` 59, `start_charging` 48, `parse_batt_info` 130, `can_tbat_do_charge` 37, `update_work_mode_para` 28) these SMALL functions carry hidden behaviour, ranked:
1. `mca_quick_charge_shutdown` (= shutdown_for_hw_action, stock +0x3b28): stock disables both CPs (`cp_set_charging_enable(0/1, 0)`), disables the CP ADC, restores the default switching frequency (mode 2 aware), sets the 5000 mV request and calls `req_adp_volt_and_cur` ("stop req volt failed" on error), then `enable_buck_charging(0,0,0)` and `fg_set_fastcharge(0)`; ours has 2 of ~16 facts: the hardware-fault shutdown does not turn the charge pumps off and does not drop the adapter to 5 V. HIGH for the quick-charge path.
2. `mca_quick_charge_chg_disable_voter_cb` (+0x5a6c) and `mca_quick_charge_chg_en_voter_cb` (+0x5b1c): stock sets a flag (+2616 = 1) and clears the `pmic_temp_term` overrides on vterm and ICL (if `support_pmic_vterm_dynamics_adjust`, +1160); ours does neither (stale overrides persist: safe-side).
3. `strategy_quickchg_if_set_input_suspend` (+0xc61c): stock clears three state words (+1932, +2913, +1180) on a suspend request; ours none.
4. `strategy_quickchg_enable_buck_charging` (+0x66e4): stock stores `buck_parallel_on = 1` (+2804) when buck parallel charging is enabled; ours has no such store (the flag is read elsewhere).
5. `stategy_quickchg_is_pdo_9v` (+0x61d0): stock tests `real_type == 11` and `mca_strategy_is_support_pps_100w()`; ours lacks the pps-100w branch.
6. `smartchg_delta_fv_callback`: stock clamps `delta_fv <= 20` on hwid platform_version 3 / major 1; ours no hwid clamp (platform_version 2 on dada: inactive).
7. `mca_quick_charge_create_group`: sysfs attribute table size 10 in stock, 9 in ours (one sysfs node missing).
8. log level only: `div4_single_voter_cb` err vs info; `smartchg_update_baa_para` info vs debug.

### 3.10 bq27z561 (REPORT ONLY; 95 differing, 44 stock functions missing)
20 differing accessors are class (a) (`fg_get_over_peak_flag`, `fg_get_current_dev`, ..., `fg_read_df_check`), 75 are not equal. The first-order hidden behaviour: the stock read accessors (`fg_read_volt` +0x24b8, `fg_read_current` +0x2780, `fg_read_temperature` +0x2a24, `fg_read_rsoc`, `fg_read_cyclecount`, `fg_read_fcc`, `fg_read_soh`, `fg_get_chip_ok`) take the `mutex_trylock` / `usleep_range(40,50)` path, test the OTA flag (+1156), and apply range checks and clamps that ours lacks: voltage `> 5000` -> "volt out of range", current outside -31000..32768 -> "current out of range" (error and no store), temperature outside the table -> "temperature out of range", volt clamped `>= 0`. The fuel-gauge values our module reports are therefore not sanity-checked the way the stock ones are. Missing functions (44, all risk paths untouched): `fg_read_block`, `fg_term_voltage_update_work`, `fg_fw_modify_ivterm_fun_P11`, `fg_ver43_update_ver_P11`, `fg_fw_clound_update` / `fg_clound_update_workfunc`, `nfg1000_*` OTA chain (erase/program/CRC/exit-boot/judge), `fg_set_force_sim_soh`, `fg_toggle_co`, `fg_read_packvolt`, `fg_read_status`, `fg_get_batt_abnormal_info`, `GetCRC8`, first-usage/manufacturing-date/vsiloss/iaver getters and setters.

## 4. Fixes made (all with stock evidence; each module rebuilt with real Kleaf, compare_ko_v2 + preamble_cmp + symcmp4)

| file:line | stock | change | result |
|---|---|---|---|
| `mca_strategy_buckchg.c:845-858` | `.text+0x5d2c` | `master_batt_missing` override release moved into the `value == 0` branch | function byte-identical |
| `mca_strategy_buckchg.c:2912-2935` | `.text+0x6db8` | `pmic_temp_term` override clear only when `value != 0` | byte-identical |
| `mca_strategy_buckchg.c:2974-3000` | `.text+0x6c2c` | same, only when `temp_value == 0` | byte-identical |
| `mca_basic_wireless.c:4155,4174` | probe `__kmalloc(...,0xdc0)` | kzalloc for tx_q1 / tx_q2 | class_probe facts 456/456 |
| `mca_quick_wireless.c:539` | `get_parallel_volt_para` | `mca_log_err` | byte-identical (with the next) |
| `mca_quick_wireless.c:1163-1167,1176,1201,1314` | `select_stage` +0x3554, `can_tbat_do_charge` +0x3b24 | `zone_changed` set / consumed, parall stage `!=`, no extra `get_parallel_volt_para` | can_tbat and get_parallel_volt_para byte-identical |

Real-Kleaf totals after the fixes: buckchg 48 of 61 byte-identical (was 45), basic 63 of 73 (unchanged), quick 29 of 37 (was 28); kCFI 45/45, 58/58, 18/18; preamble_cmp 0 diffs. All three stay PARTIAL.

## 5. Ranking of the class (b) findings by battery-safety impact (what is still open)

1. quickchg `shutdown_for_hw_action`: CPs stay on, no 5 V request after a hardware action (module owned by G13n).
2. bq27z561 read accessors without range checks / clamps (voltage, current, temperature accepted unchecked).
3. quick_wireless `process_event`: no `fg_ota_process` vote (wireless quick charging continues during fuel-gauge OTA), no CP_NEW_MODE stop.
4. quickchg voter callbacks / `if_set_input_suspend` / `enable_buck_charging`: stale pmic_temp_term overrides and state words (safe-side).
5. basic_wireless `process_irq` DFX mapping and the 20 V/32 V TX range (platform_version 3 only; dada = 2).
6. FIXED here: buckchg `master_batt_missing` override released on "battery missing" (was HIGH-MEDIUM), the two `pmic_temp_term` clears on disable (LOW), quick_wireless stage re-selection after a temperature zone change (LOW-MEDIUM, safe-side), tx_q buffers (none).
7. base-flip-only differences (select_max_ibat, set_flip_cur): inactive on dada.

## 6. Caveats

- "class (a)" is proven by symbolic facts (effects and their guards), not by running anything; stack-data facts and phi members are partly noise and were compared only after label normalisation. fg_update_status / fg_monitor_workfunc / buckchg class_probe have 100+ moving stack facts that I did not check one by one.
- The classification of `process_event` / `get_status` in buckchg and `process_event` in basic_wireless rests on the equality of all effect facts after phi normalisation; the jump-table phi merges of the tool are not exact.
- Artefacts: server `<work-dir>` (reports `*_cmp.txt`, `*_sym.txt`, `*_s4.txt`), tools `work/re/g13o_tools/{kb.sh,kbs.sh}`, `work/a17/review/tools4/symcmp4.py`. bazel-out of every build deleted; no stub `.ko` was shipped.
