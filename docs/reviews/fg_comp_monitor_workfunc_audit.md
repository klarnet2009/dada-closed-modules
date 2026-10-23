# fg_comp strategy_fg_monitor_workfunc: block-by-block audit against GLOBAL OS4.0.0.7 stock (goal-G25c, 20.10.2026)

STATUS: community-only, DO NOT LOAD, never flash / never in a ROM build or the kernel package. Battery-safety relevant module: NO safety claim.
Not hardware-tested. This is a read-through of disassembly vs source, not a proof by a tool; guesses are marked [GUESS].

Object: `strategy_fg_monitor_workfunc` of mca_strategy_fg_comp.ko, stock `.text+0x1ee0`, size 0x1fc8 (2035 instr.), ours 2028. All inlined callees
(check_soc_smooth, check_termination, parallel_termination, parallel_first_termination, get_term_current_new x2, single/parallel force_fw_report_full,
check_recharge, check_fl4p0_status, check_lossless_recharge, check_fast_mode_times, qbg_send_chg_data, onewire_batt_auth, charge_slowly_monitor_func,
charge_get_batt_rm_threshold, get_ffc_safe_item, adjust_interval, reset_co_to_default) were read as part of this function.
Not inlined in either build (out of this audit, called with equal arguments): strategy_fg_init_voter 4634, fg_check_batt_auth 5498 (x2),
fg_update_status 55dc, check_batt_emerg 680c, check_sigle_termination 6ca8, parallel_force_fw_report_full 6f48 (x2), start_recharging 7338 (x3),
get_fl4p0_iterm 73d0, mca_strategy_check_li_plating, mca_strategy_start_isc_detect_2_work.

Method: stock disassembly normalised (offsets relative to the function, relocs resolved, .rodata strings resolved, `[x19,#n]` = fg+n+8, `[x20,#n]` = fg+n
annotated with BTF field names from g14e_tools/dump_mca_strategy_fg_comp/btf_all.txt, cfg sub-fields too) and read top to bottom against the source
(mca_strategy_fg.c 3811-3921 + the inlined functions). Every condition (OR/AND terms, constants, signed/unsigned, polarity), every store to the info
struct (offset -> field), every call and its constant arguments, and the order of side effects were compared. In addition a call+constant-argument
multiset of stock vs our build: 161 calls = 161 calls, all `mov #imm` arguments, log line numbers and strings identical (tool g25c/calls.py).
Scratch: <work-dir> local helper files in the session scratchpad (g25c/).

Result: NO proven BEHAVIOUR difference in any block. Every block below is CODEGEN-or-EQUAL. The only source-level differences found are
two address-of vs member-access forms (array-bounds trap bound), edited as codegen fixes (section 3).

## 1. Block table (stock offsets are `.text+0x1ee0+off`)

| # | stock off | source | what | compared | verdict |
|---|-----------|--------|------|----------|---------|
| B1 | 0x00-0x7c | 3811-3830 | prologue, zero-init of locals, hwid | memset 500 B, locals zero; `event_data` 16 B zero | EQUAL |
| B2 | 0x80-0x14c | 3832-3851 | `!fg_init_flag`: probe_ok(master/slave), `fg_type != SINGLE`, flags into fg+0xd24/0xd25 (fg_master/slave_init_flag), log 2862/2872/2874, `flag != 0` -> fg_check_batt_auth + requeue 250 jiffies (=msecs_to_jiffies(1000) at HZ 250) | polarity, order of flag |= , stores, line numbers | EQUAL |
| B3 | 0x228-0x288, 0x680 | 3852-3862 | boot_update_soc=true, update_batt_info; dual: fg_init_flag = master&&slave, `support_base_flip || base_flip_same` -> queue reset_default_co_work (fg+0x558); single: fg_init_flag = master_ok | cset/ccmp form = AND, fg+0x558 = reset_default_co_work (BTF 1368) | EQUAL |
| B4 | 0x688-0x704 | 3863-3880 | get_fg_vendor, hwid && country_version(+4)==CN(0) && support_fl4p0 -> fl4p0_enable_check; support_dod_vcutoff_v2: vendor==4 -> queue update_vcoutoff_v2_work (fg+0x4d0) 1250 jiffies (=5000 ms), else log 2900 + v2=false, vcutoff=true | offsets 3073/3071, constants | EQUAL |
| B5 | 0x84-0x16c | 3882-3889 | `!voter_ok` -> init_voter; update_status(fg, !(resume\|\|boot)) | w1 = 0 when resume or boot flag set | EQUAL |
| B6 | 0x170-0x2e4 | 3890-3893 + 3600-3626, 3725-3751 | support_qbg: onewire_batt_auth (log 2785 if !batt_auth), qbg_send_chg_data: get_status(1,2/5/0x19/0x1a), voter results to chg_data+4/+8/+12 or 500/565/4480, chg_data.quick_chg_en = (x==1), +16 = chg_status, get_status(7,0x13) -> `!x` -> qbg_send_chg_data(0,&chg_data) | offsets (fg+0xd70..0xd80), constants, polarity | EQUAL |
| B7 | 0x2e8-0x308 | 3894-3898 | base_flip_same: get_status(7,0x11,&cold_zone) -> fg+0xd34 | | EQUAL |
| B8 | 0x308-0x39c | 3900-3915 | charging_done_time >= 100 s: soc_not_full report (ui!=100 \|\| eu) && rsoc!=100, params {voltage, ui, rsoc}, event 11, store 0 before log 2934 | all 3 polarity paths (ui!=100 / ui==100&&eu / ui==100&&!eu) | EQUAL |
| B9 | 0x39c-0x404 | 3917-3927 | not_stop_chg_err: now-last>=100, ui==100 && eu && chg_status==1, {current, voltage, temp, cycle}, event 44, last=now | | EQUAL |
| B10 | 0x404-0x5b0 | 3928-3961 | batt_capacity: RM/FCC log 2953 (6 args), `batt_auth && !fg_error && (rm>=dc*11/10 \|\| fcc<1000001 \|\| fcc>=dc*11/10 \|\| enable_fcc_default)` -> event 42 with 8 params + log 2967; raw_soc jump >= 3001 (init_flag set) -> uevent "BATTERY_RAW_SOC_JUMP=1" len 22 + log 2976; stores of last_* + init_flag | each OR term located (4a0-4bc), `dc*11/10` = smull/10 signed, abs via cneg | EQUAL |
| B11 | 0x5b0-0x818 | 3962-3963 + 3752-3794 | support_new_soc_smooth && cycle >= drop_fv_cycle -> check_soc_smooth: eu/NULL gate, get_status(1,0x20), log 1943, chg_done_cnt>=2 && ui!=100 && !allow -> allow=1, gate power_present && drop_done && ui!=100 && !flag, start_pmic time, time_delta>29, voter iterm, `-ibat/1000 < iterm+2000`, charging_done branch, logs 1971/1973 | the `cmp #0x1d; b.gt` = `> 29` (G22d), +2000, normal_iterm 250 (0xfa) | EQUAL |
| B12 | 0x818-0x8e0, 0x834-0x1260, 0x1de4-0x1f78 | 3965 + 3168-3266, 3044-3112, 3125-3160 | check_termination switch: single -> call, parallel(1) -> inlined parallel_termination + first_termination + get_term_current_new x2 (last_temp_index .data+0x1f0/4, last_term .data+0x1e8/c), series/default -> nothing. Gate power_present && !charging_done && vterm>=4400; soc-lock logic (tem>5, soc_delt<tem-1, tem<=4 clears lock); fc/co logic with 1384/1389/1396/1401 logs; vterm_target add table (20/0/65/50/20/60/30, `!fast -> 0`); iterm vote para_iterm +40; term-current table search; dual_term_curr / final_term_curr; process(0|2, 0x14); high_temp_para 200 (temp>400); para_term vote | **dead on dada: fg_type = 0 [VERIFIED earlier]**; read for equality anyway: constants, polarities (unsigned `hi` for temp>399 only reached for temp>=300), data initial values via compare_ko_v2 objects PASS | EQUAL (not reachable on dada) |
| B13 | 0x8e0-0x1318 | 3967 + 3267-3438 + 3439-3460 | force_fw_report_full: fg NULL / fg_init_flag gate, single_force_fw_report_full: gates (power_present, rsoc>=90, fast\|\|normal_force_full, cycle<drop_fv), get_status(2,0,&online) -> (online?3:1, 2), voter iterm/vterm else 250/4400, vterm<=4150 reset, terminated_by_cp 3 branches (13/24 >3000 w/ max(.,500)+x, 113/124, +120; 115/120 >999; hys), count_threshold 3/2, logs 1627/1642/1658/1666/1675/1676/1681/1687/1694, reset when charging_done, `-ibat/1000<force_iterm && vbat>force_vterm && !start_force_full`, count++ >= thr -> set_force_report_full + queue force_report_full_work 2000 jiffies (=8000 ms), else count=0 and cur_time>=7 -> done_force_full; parallel variant (soh/cycle diff, `%50` by mul-inverse, flags [0]/[1], log 1841, two parallel_force_fw_report_full calls) | all paths incl. polarity of `soh_diff<0`, `cycle_diff>0` | EQUAL |
| B14 | 0x1318-0x1330, 0x1a84-0x1ac0, 0x1b38-0x1ba0 | 3463-3488 | check_recharge: `!power_present \|\| !charging_done \|\| recharging` -> 0; non-eu: raw_soc<=report_full_raw_soc && ((smooth_active && vbat<vterm-50) \|\| (!(smooth_active && ui==100) && !allow)); eu: ui<95 (cmp #0x5e b.gt); logs 1917/1922 | boolean algebra expanded on the 4 leaf paths | EQUAL |
| B15 | 0x1330-0x1348 | 3977-3978 | single && support_isc_li_plating -> check_li_plating | | EQUAL |
| B16 | 0x1348-0x13ac | 3979, 3796-3809 | fast_mode_times: get / set(cfg value) when power && !x, set(0) when !power && x, log 1885 only inside the `support_fast_mode_when_charging` gate | | EQUAL |
| B17 | 0x13ac-0x1400 | 3980-3989 | rechg_soc_err: >=100 s, recharging && eu && ui>=96, event 43, {ui, vbat, temp, cycle}, last=now | | EQUAL |
| B18 | 0x1400-0x1d04 | 3990-3991 + 3490-3534 | `hwid && CN && support_fl4p0` -> fl4p0: vote calibration (ffc_safe_item[cycle<=100/300/500/800][temp <200/250/300/350/400/450], table at cfg+448) / false,0; gate (!fg, charging_done, recharging, !voter_ok, !fast, ffc_continue); `-ibat/1000<iterm && vterm>=4400 && vbat>=vterm-11 && !ffc_continue` -> count++ >= 2 -> ffc_continue; log 2108; calibration_temp; real_supplement==0xFFFF -> log 2117 + set_charging_done(true) else log 2120 + queue fl4p0_calibration_work (fg+0x228) delay 0 | | EQUAL |
| B19 | 0x140c-0x1604 | 3992-3993 + 3536-3598 | lossless: ktime_get, !charging_done -> last=now, eu cycle<=300 -> disable + log 2142, input_suspend votable find/effective -> log 2150, time_diff = ns/1e9 (= ktime_to_ms/1000 for t>=0), get_temperature/10, `done && (fast\|\|need) && temp_low<=t<=temp_high && diff>=rest_time` -> need=false, lossless=true, votes 600/100 (base_flip) or lossless_curr_limit; log 2174; lossless -> start_recharging + log 2178 | cfg.rechg_cfg +0/+4/+8 = fg+0x880..0x888 | EQUAL |
| B20 | 0x1604-0x1610 | 3994-3995 | fg_check_batt_auth, check_batt_emerg (call, equal args) | order | EQUAL |
| B21 | 0x1610-0x1938 | 3627-3692 (+charge_get_batt_rm_threshold 3612-3624) | charge_slowly_monitor_func: logs 4764/4765, type 12 (PD_VERIFY) ui<=80: online_time, adapter_max_power(PPS), rm_thr (420/200, >100000 -> 500, `>= 67000`), logs 4748/4785, power>=33 && dc>=thr*1000, xmpps init, 600 s window, `now_soc-initial_soc < thr*100/(dc/1000-100)` -> logs 4799-4801, event 37 (5 params), reload; else type 1..11, !nonxmpps_flag, >=61 s -> event 38, flag, log 4773; reset of xmpps_initialtime/nowtime/now_rm/now_soc | | EQUAL |
| B22 | 0x19c4-0x1a50 | 3997-4009 | single && support_isc_li_plating: isDetecting && !need_stop && ibat>=61000 -> log 3019, need_stop; rsoc>=41 && !screen && !usb && !wls && !avg_done_cnt && !isDetecting_2 && !collect_done_2 -> start_isc_detect_2_work | field offsets (isc_det_info +0/+1, isc_det_2_info +0/+12/+20) | EQUAL |
| B23 | 0x1940-0x1984 | 4011 + 3694-3723 | adjust_interval: update_period != -1; near_vterm/charging/fast/base_flip_same&&soc>=96&&cold_zone = 1000, else 5000; not charging 10000/30000, `vbat_mean < shutdown_delay+200` -> temp<0 ? 2000 : 5000, else temp<0 -> 5000; `vbat < vcutoff_sw+50` -> 1000 | | EQUAL |
| B24 | 0x1984-0x19c0 | 4012 | mca_queue_delayed_work(&monitor_work, msecs_to_jiffies(update_interval)) | | EQUAL |

## 2. Differences between stock and our binary that remain (all CODEGEN)

- register allocation / block order / instruction scheduling (2035 vs 2028 instructions), stack frame 0x2a0 vs 0x290;
- table pointers `+0x930/+0x940` held in a register vs reloaded, the 1e9 divisor materialised once vs twice, bool reloads, tail-merged `strb 0xdd1`;
- `cmp #0x1d; b.gt` vs `cmp #0x1e; b.lt` (time_delta > 29), `ldrsw` vs `ldr` of a value used as 32 bit;
- call multiset: 161 = 161, identical constant arguments, identical log strings and line numbers.

## 3. The li_plating / isc trap bound (0xa vs 0xb, 5 vs 6): cause found

Stock `strategy_li_plating_work` and the inlined `report_li_plating_data` check `idx <u 10`; our build `idx <u 11`. In `isc_do_point_sample_2` stock `pos <u 5`, ours `<u 6`.
Cause (clang UBSan array-bounds semantics, [INFERRED from the codegen difference and from clang's rule that `&a[i]` is allowed to point one past the end, i.e. the check is `i <= N` for
an address-of and `i < N` for an access]): our source took `d = &info->li_data_1[idx]` / `d = &isc_data[group][pos]` (address-of -> bound N+1), the stock source indexes the array
directly in a member access (`info->li_data_1[idx].temp = ...`, bound N). It is not a build-flag difference and not range folding (the G23c explanation was wrong: the loaded
`before_full_index` in report_li_plating_data has no range). Behaviour: identical for every reachable index (trap only on a corrupted index). Source edited (member accesses) in
mca_battery_safe.c (report_li_plating_data, strategy_li_plating_work, isc_do_point_sample_2).
Rebuild check (real Kleaf, scratch <work-dir> .ko not shipped): bounds now equal (`cmp #0xa` x2 in li_plating, `#5,#5,#0xa` in isc_do_point_sample_2); strategy_li_plating_work 331 = 331
instructions, symcmp5 CLEAN-POL (one `stp xzr,xzr` placed differently; before: POLARITY-DIFF 33); isc_do_point_sample_2 keeps an ordering difference of loads in the average loop
and `ldrsw x22` vs `ldr w22` (CODEGEN, not iterated further); fg_comp 19 of 99 functions still differ (same count), function-set / kCFI / objects / anon-data / imports / exports PASS, preamble_cmp 0.
monitor_workfunc itself after the rebuild: symcmp5 POLARITY-DIFF 8 (all stack-argument slots of the mievent arrays and get_status out-variables, same atoms, no one-sided atom; 57 compare atoms pair 1:1 with equal constants).

## 4. Not done / limits

- Not hardware-tested, no gauge, no stock dmesg; reading is by a human-style pass and can still miss a term: the 161-call multiset and the earlier symcmp5 runs are cross-checks, not proofs.
- Functions called (not inlined) by the monitor function were not re-read here: fg_update_status, strategy_fg_init_voter, fg_check_batt_auth, check_batt_emerg, check_sigle_termination, parallel_force_fw_report_full, start_recharging, get_fl4p0_iterm, check_li_plating (G23c read some of them).
- Parallel/dual-FG paths (B12, B13 parallel part) are dead on dada (fg_type 0), read for equality only.
- 17 of the 19 differing functions in fg_comp were not re-read in G25c.
