# Charging family: every function that still differs in bytes from stock (G40e, 20.10.2026)

COMMUNITY-ONLY, DO NOT LOAD, never flash, never in a ROM build or the kernel package. NO safety claim: this is a static read, nothing was run on hardware.
Base = GLOBAL OS4.0.0.7 stock (`<work-dir>`). Nothing was rebuilt and no source was changed by G40e (no proven BEHAVIOUR difference was found), so the md5 of
the compared builds are the ones of the latest builds of the earlier goals (below).

## Method (RAW listings, not the lifter sx.py)
1. `compare_ko_v2.py -v` of stock vs the newest build of every module (`<work-dir>`): function-bytes list = the list below.
2. For each differing function the compact listing (`lst2.py`: raw instructions, relocation targets/strings/fields resolved, function-relative branch targets) of stock and ours.
3. Block-wise alpha-rename diff `alpha3.py` (registers renamed by first appearance inside each basic block, statics/local functions/adrp pages normalised, `ldapur`->`ldar`):
   only the DIFFERING hunks are read (equal blocks are identical instructions modulo register names). Tools: `work/re/g40e_tools/`.
4. Every hunk was classified; for every hunk that touches a condition, a constant, a call, a store or a log the CONTEXT was read side by side in the raw listing
   (`rng.py m f lo hi`), i.e. the whole decision chain was followed (OR/AND terms, signedness, polarity, polarity-inverted helper registers, store order, arguments).
   Hunk-feature screens `sus2.py`/`sus3.py` (multiset of immediates/calls/strings/condition codes per hunk) were used ONLY to find hunks that needed a closer look, never as a proof.
5. Verdict classes: EQUAL-READ = raw context read, same behaviour (CODEGEN only: registers, spill slots, block layout, scheduling, ccmp/csel forms, tail merge, inverted branch with swapped fall-through);
   SCREENED = hunks screened by features, every flagged hunk read, but not every instruction read (stated per row); COVERED-BEFORE = read line by line by an earlier goal (named), here only re-screened.
   BEHAVIOUR = would change what the code does. NONE was found in G40e.

Lifter warning kept: sx.py was NOT used for any verdict here.

## Builds compared (md5 of the .ko; sources unchanged by G40e)
quickchg 5d356a07b27494eff0ddba68b98a852e (g31a), basic_wireless 52b4524fe7ab729c73a637ad5628e87e (g31a), fg_comp 324f523defc0729e3efdbddcdf7436b1 (g31a),
quick_wireless 5045d7d40a276dd1e71900db9098922c (g30e), business_charger_comp 085d0d6b9c1c7f3ae47a31af9e96319f (g25b), buckchg b8e1157d26dcbac74ef2de06c5990392 (g30e, source = g25b),
smart_charge b7e7e41f8eb1475b4e39fb6c13873930 (g25b), buckchg_jeita edea5e1deb73c4632e1fe7587774e0c9 (g25b), wireless_revchg 0b814fb7495dcc5dda1da2e76306f352 (g25b),
connector_antiburn 52f6c6256c23ac86d53e8fd92d45edb3 (g20a), pwm_fan 7d20fa4ee56363e19e2240602823f309 (g20a), charger_partition 89d56aa886d08dbecb98d0a62e41b2b4,
platform_fg_ic_ops 280455ff6002a3a85f0d61800809aa36, strategy_fg_class a8fc25e471c7d4f4c629aa30ad8e34e7, charge_mievent 389468cb84ecf7c177f4e90c351514db (g14b).
Byte-identical modules (all functions identical, nothing to read): charger_partition 42/42, mca_platform_fg_ic_ops 81/81, mca_strategy_fg_class 33/33, mca_charge_mievent 2/2.

## Table (module, function, insns stock/ours, hunks, verdict, evidence, fixed?)

### mca_smart_charge (5 of 38 differ) - all EQUAL-READ
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| smart_charge_sysfs_store | 122/121 | 2 | EQUAL-READ CODEGEN | `bl mca_get_hwid_info` returns x0 in both; the load of `*buf` / adrp tbl / `mov w2,#0xb` / `str wzr,[sp+4]` are scheduled around the call differently; same args to mca_sysfs_lookup_attr (0x20..0x6c both listings read) | no need |
| smart_charge_check_bypass_status | 64/65 | 5 | EQUAL-READ CODEGEN | once_flag is loaded once (stock) vs reloaded before `eor`(ours): no store in between, race-only; tbz vs cbz of the same flag; spill pair | no need |
| smartcharging_handle_algorithm_conflict | 76/77 | 2 | EQUAL-READ CODEGEN | exit `mov w20,wzr` placed on the shared tail vs duplicated block (0x118/0x154 both read) | no need |
| smart_charge_handle_sense_chg | 134/133 | 2 | EQUAL-READ CODEGEN | stock `cmp w7,#6; ccmp w7,#3,#4,lo; csel ne` vs ours `ccmp w7,#6,#2,ne; csel lo` both give 4 iff w7 in {4,5} (derived by hand) | no need |
| smart_charge_workfunc | 321/319 | 8 | EQUAL-READ CODEGEN | `str wzr,[sp+0x10]` before/after `add x0`; cbz w21 / cbz x20 order (pure tests, same target); `cmp #0x2bc`/`b.eq` swap order of 3 equality tests with the same target; mov w22/w24 order | no need |

### mca_wireless_revchg (2 of 45) - EQUAL-READ
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| mca_wireless_rev_charge_config_work | 145/145 | 1 | EQUAL-READ CODEGEN | `str w8,[x19+0x810]` scheduled before/after the snprintf arg setup (independent) | no |
| mca_wireless_update_fw_mainthread_work | 226/226 | 2 | EQUAL-READ CODEGEN | `str xzr,[sp+0x20]` (fail_info=NULL) at function top (stock) vs right before the get_fw_upgrade_fail_info call (ours): the block is not in a loop (1b8 is the common exit); same value reaches the call | no |

### mca_pwm_fan (4 of 20) - EQUAL-READ
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| fan_sysfs_store | 142/142 | 1 | EQUAL-READ CODEGEN | `(a!=0)==(w!=0)` as `cset eq; eor; tbnz` vs `cset ne; cmp; b.eq`, derived | no |
| set_pwm (__set_pwm inlined) | 155/155 | 9 | EQUAL-READ, one non-observable order difference | whole function read in both listings: all conditions/constants/log lines/returns equal; ours returns constant 0 on the success tails where stock returns the pwm_reg_config result (clang knows pwm_reg_config returns 0 or <0; function is byte-identical to stock); stock stores `ctx->pwm_duty` (32-bit) BEFORE the `new duty` log, ours after the log and logs the 64-bit duty: same text for duty < 2^32 (a percent), mutex held | no (not observable) |
| set_pwn_fan_target_level | 143/143 | 4 | EQUAL-READ CODEGEN | `cmp #4; b.hs` vs `cmp #3; b.hi`; `cset eq/eor` vs `cset ne/cmp` as above; log block moved | no |
| speed_detect_req_work_func | 57/57 | 2 | EQUAL-READ CODEGEN | zero-init of the on-stack struct (stp xzr,xzr vs str), x0/x1 naming | no |

### mca_connector_antiburn (5 of 17) - EQUAL-READ (probe: pairing read, spill slots not individually traced)
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| antiburn_sysfs_show | 74/72 | 6 | EQUAL-READ CODEGEN | lookup_attr args, case-0 (`temp*10` if flag==0) identical; field `[0xfc]` read before the call (stock) vs after (ours): get_temperature has no store to it (listing read) | no |
| antiburn_sysfs_store | 99/98 | 2 | EQUAL-READ CODEGEN | w20 vs reload of `[sp+4]`, same value, same arguments (0x118..0x168 read) | no |
| connector_antiburn_get_temperature | 86/85 | 3 | EQUAL-READ CODEGEN | adc_err_flag=1 store scheduled before/after `ldr w0,[x8+0x84]`; reset block: both write adc_err_flag=0 and flag_restore=1 under `flag==1` | no |
| connector_antiburn_monitor_workfunc | 901/902 | 8 | EQUAL-READ CODEGEN | `cmp #0xd6d8; b.ge` (stock) vs `cmp #0xd6d7; b.gt` (ours) = same predicate; bool flags: `tbz bit0` vs `cmp #1; b.ne` on 0/1 values; loads of the flag placed later (race only) | no |
| connector_antiburn_probe | 478/480 | 24 | EQUAL-READ for the constant/condition part, SCREENED for spill slots | all 9 defaults (0xfde8/0xd6d8/0xfa0/0x37/0x3c/0xea60/0x4/0x23/0x88b8) paired by csel exactly as in stock (non-elab, elab): trigger 65000, recharge 55000, rate 4000, comb_sensorboard 60000, comb_rate 35000; field offsets 0xac/0xb0/0xcc/0xa4/0xa8 and names in the same order; the rest is the permutation of 7 spill slots (stur/ldr x2): the slot -> property mapping was NOT traced instruction by instruction | no |

### mca_business_charger_comp (4 of 45) - EQUAL-READ
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| business_charger_probe | 307/307 | 3 | EQUAL-READ CODEGEN | `of_find_property(np,"keep-report-max-power")`: np reload of `[x20+0x310]` vs held in x20; spill pair | no |
| business_charger_report_quick_charge_type_work | 102/98 | 9 | EQUAL-READ CODEGEN | whole function read (0x20..0x140 both): max(cur,power) for keep flag, thresholds 0x31/0x1d/0x13, types 4/3/2/1, 100W-PPS branch with csinc 3/4: identical; ours has the tails merged | no |
| business_charger_event_thread | 802/803 | 2 | EQUAL-READ CODEGEN | `mov w2,w3` placement before the shared tail (0x880..0x8a0, 0xca0..0xcc0 read) | no |
| usb_psy_get_prop | 119/118 | 12 | EQUAL-READ CODEGEN | whole function read: only the stack slots of curr/volt/pair are permuted consistently, zero-init covers the same two ints; same props/returns/logs | no |

### mca_quick_wireless (3 of 37) - EQUAL-READ
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| mca_wireless_quick_charge_select_max_ibat | 256/256 | 2 | EQUAL-READ | the csel/ccmp chain derived on both sides: result = (w24>w25-30 && last!=-1 && last<w8) ? last : w8 in both | no |
| mca_wireless_quick_charge_monitor_work | 787/788 | 8 | EQUAL-READ (inlined is_charge_abnormal) | the helper register w25 has inverted polarity in ours (1=abnormal vs stock 1=normal): every def (0x7c/0xcc/0x118/0x144 vs 0xc8/0x114/0x144) and both uses (`cbz w25` vs `tbnz w25` at 0x22c/0x230) checked; counters/logs equal | no |
| mca_wireless_quick_charge_parse_batt_info | 476/478 | 9 | EQUAL-READ for the base/flip ffc block; SCREENED for the rest | stock: `x3=base`, `==0 ? base : ==1 ? flip : error`; ours: csel (==0 base else flip) + `cmp #1; b.hi` error: same for all values; spill slots 0x54/0x58 vs 0x5c/0x50 consistent in all stores and reloads | no |

### mca_buckchg_jeita (4 of 17) - EQUAL-READ for monitor_work blocks listed, rest SCREENED
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| mca_buckchg_jeita_probe | 460/459 | 7 | EQUAL-READ CODEGEN | zero-inits reordered; one log string reached via .rodata.str1.1+addend (lst2 cannot resolve): both strings present | no |
| mca_buckchg_jeita_monitor_work | 1128/1135 | 35 | EQUAL-READ for: batt-spec flag (w27 vs stack slot), fcc/last_type block, hys flag w28/w25/w23 polarity-inverted pair (all defs and the store condition `!w23 && !w28` vs `w28&w25` traced), swocp counter/vote (count++ / `>=4` / vote -100 / log / runswocp, and the `500 - w24` disable), cur-index/vbat csel pairs; SCREENED (feature multiset per hunk) for the flip_jeita tail | the only order difference: base_over_curr_count zeroed after (ours) vs before (stock) mca_vote: the static is private to this function, not observable | no |
| mca_strategy_buckchg_jeita_get_status | 248/249 | 2 | EQUAL-READ CODEGEN | `cur_jeita_index` read once, `*status=0` store order independent | no |
| mca_buckchg_parse_dynamic_iterm_by_name | 161/161 | 7 | EQUAL-READ CODEGEN | locals at sp+0/sp+0xc vs sp+8/sp+4, consistent | no |

### mca_strategy_buckchg (11 of 62)
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| strategy_buckchg_set_vterm | 131/128 | 13 | EQUAL-READ (never read by hand before) | raw 0x120..0x1d8 both: temp and flag slots swapped consistently ([sp+4]<->[sp]); fast mode: temp>0x22 ->45, <0x21 ->40, 0x21/0x22 -> flag?45:40 (ccmp/csel form derived); not fast: `temp-0x30 <= 6 (unsigned)` -> 30 else 0 stored to +0x64; w1 = w8 + w20 both; set_term_volt path equal | no |
| strategy_buckchg_cp_revert_handler | 269/269 | 6 | EQUAL-READ | chip vendor dispatch: stock `==2 -> start revert 1_2 cp`, `==1 -> external boost`; ours `==1 -> external boost` (0x2c0), `==2` fall through to `start revert 1_2 cp`: blocks only moved; sequences of cp calls equal | no |
| strategy_buckchg_process_csd_pulse | 53/52 | 2 | EQUAL-READ CODEGEN | block moved | no |
| strategy_buckchg_get_status | 129/130 | 2 | SCREENED | hunks have the same constants/calls/strings | no |
| stategy_buckchg_is_pdo_9v | 117/117 | 4 | SCREENED (log block moved, flagged hunks read) | stores `[x5+0x18b]=1`, `[x5+0x184]=w6`, log "can support 9v" identical | no |
| strategy_buckchg_base_flip_sw_cv_workfunc | 183/183 | 7 | SCREENED | constants -4/25 swapped between registers only; the 500-clamp (`cmp #0x1f4; csel gt`) identical | no |
| strategy_buckchg_monitor_workfunc | 1525/1525 | 33 | PARTLY READ | read: 0x3f0..0x470 (fast-charge skip chain: stock `w8!=0 || w25&1 || w21>0x5e`, ours same set), the 0x1868/0x18b8 cold-hot log block (moved), the pps_100w/0xb chain (same instructions, other order); the rest SCREENED (G22d/G25b symcmp5: CLEAN-POL) | no |
| strategy_buckchg_class_probe | 1214/1213 | 36 | SCREENED + COVERED-BEFORE (G31a: jump tables, hwid cset) | only string-ref/`.rodata` form differences and register allocation in flagged hunks | no |
| strategy_buckchg_process_event | 384/379 | 8 | SCREENED + COVERED-BEFORE (G31a call names) | | no |
| strategy_source_status_monitor_workfunc | 251/253 | 21 | SCREENED (4 cond-flagged hunks read: `b.ge`/`b.lt` with moved fall-through, `subs #1;b.eq` vs `adds #1;b.hs` loop counter: 15 iterations both) | G22d hand read | no |
| strategy_wls_revchg_monitor_workfunc | 241/242 | 12 | SCREENED (2 flagged read: `ccmp` vs split branches, `cmp #5;b.lo` vs `+1;cmp #6;b.ne` = same loop bound 5/6 form, G22d) | | no |

### mca_basic_wireless (9 of 73)
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| strategy_class_wireless_ops_get_adapter_charger_mode | 70/70 | 3 | EQUAL-READ | both whole functions read: modes 2/4/-1, the chain (0x10a0 range 9..13, 14..16 uuid 0x0e0e0205 test, 0x124==2, per-index 0x1e4/0x1f0) equal, only `b.hs`/`b.lo` inversion and tail layout | no |
| strategy_wireless_adapter_handle | 339/339 | 7 | EQUAL-READ | the two stores `[x19+0x10e0]=w20` / `[x22+0x12]=w23` swapped in order (different locations); same for the second copy | no |
| strategy_wireless_split_irq_index | 79/78 | 3 | EQUAL-READ CODEGEN | `1<<n` stored after the log vs before | no |
| strategy_wireless_sysfs_store | 264/264 | 2 | EQUAL-READ CODEGEN | spill order | no |
| strategy_wireless_monitor_work | 593/593 | 2 | EQUAL-READ | loop counter `15, subs #1;b.eq` (stock) vs `-15, adds #1;b.hs` (ours): 15 iterations both | no |
| strategy_wireless_buckchg_base_flip_sw_cv_workfunc | 157/157 | 2 | EQUAL-READ CODEGEN | -4/25 constants swapped between registers, clamp 500 equal | no |
| basic_wireless_class_probe | 1105/1103 | 11 | SCREENED + COVERED-BEFORE (G31a: 11 bytes identical) | only string form `.rodata`, regs | no |
| strategy_wireless_process_event | 842/840 | 5 | SCREENED + COVERED-BEFORE (G30e/G31a jump tables, atoms) | no flagged hunk with changed constants | no |
| strategy_wireless_process_irq | 1328/1328 | 11 | COVERED-BEFORE (G31a fixes, count equal to stock) | the 20000/32000 `csel` vs tail merge hunk re-read | no |

### mca_strategy_fg_comp (19 of 99) - never-read-by-hand ones read here
| function | insns | hunks | verdict | evidence | fixed |
|---|---|---|---|---|---|
| mca_battery_shutdown_parse_dt (5-entry table) | 452/449 | 6 | EQUAL-READ | error paths: stock `log; b 7ac` (7ac = `mov w22,-1`) vs ours merged `log; mov w22,-1`; kfree path equal; `cmp #1;b.lt` = `cmp #0;b.le` (signed); the table constants are in equal blocks; .rodata table content already equal (G33a) | no |
| mca_strategy_get_first_usage_date | 95/97 | 6 | EQUAL-READ (parallel-FG branch, dead on dada fg_type 0) | whole function read: master/slave valid test vs "99999999" (8-byte compare), byte loop, choose master when equal or master>slave else slave; both give identical selection (derived for all 4 valid/invalid combinations) | no |
| mca_strategy_get_manufacturing_date | 126/129 | 7 | SCREENED (parallel-only, dead on dada) | same structure as the source (empty tests, memcpy, loop from byte 3, `>=` choose slave, log 0x8c5 "inconsistent"); not read line by line | no |
| strategy_fg_panel_notifier_cb | 29/29 | 2 | EQUAL-READ | `cbz w8(0x2a4)` / `cbz w20(screen)` order swapped (pure tests, same target) | no |
| report_isc_data | 122/122 | 2 | EQUAL-READ CODEGEN | stp xzr placement | no |
| report_isc_data_2 | 180/179 | 25 | EQUAL-READ | whole function read: 2 iterations of the 5-row snprintf, index 0/1, `-%d` = i+1, qmax/d_t/cc/p, fields loaded later in ours (race only); loop test `cmp w20,#3` vs `tbnz w20,#0` is a different induction variable with the same 2 trips | no |
| mca_strategy_check_batt_emerg | 118/118 | 2 | EQUAL-READ | tail layout; the G31a fix (status=0 before the log) is in the equal part | no |
| mca_strategy_parallel_force_fw_report_full | 239/238 | 4 | EQUAL-READ (parallel-only) | add/sub 0x1e/0x17 order only | no |
| mca_strategy_check_li_plating | 97/95 | 5 | EQUAL-READ | stock `ldrsw` + signed /10 (smull #34) vs ours `ldr w` + unsigned /10 (umull #35) for the log value: reached only when `cmp w20,#0x96; b.lt` (signed, same in both) passed, i.e. 150 <= v < 2^31: identical results; later compares `b.lo`/`b.lt` equal | no |
| strategy_li_plating_work | 306/306 | 2 | EQUAL-READ CODEGEN | stp placement | no |
| isc_do_point_sample_2 | 272/272 | 17 | EQUAL-READ | the two 5-term sums (`f78,f98,fb8,fd8,ff8` and `f80,fa0,fc0,fe0,1000`) contain the same 5 elements each, /10 by smull, log args avg_curr = second sum, avg_temp = first sum: same in both | no |
| fg_ui_soc_smooth | 754/751 | 26 | EQUAL-READ for the hunks with conditions | region 0x390..0x490 both: soc/out variables swapped w23<->w24, the `cmp #0x64;b.ne` check moved in front of side-effect-free loads; the sdiv/msub mapped_soc = min(q,99) and remainder store to 0xb90; log block moved; other hunks register/spill | no |
| strategy_update_vcoutoff_v2_work | 367/376 | 17 | EQUAL-READ for the iaver block | `iaver_time_flag` set/reload, 64800 s check (0x3aef6cfb4000), `allow_up`: helper w27 in ours has the opposite polarity of stock w22 (defs 0x4c8/0x528/0x534/0x548, uses `tbnz w27` vs `cbz w22`) traced; rsoc>=51 / >=10 ladder, gear update `min(eff+1,total-1)` equal; (stock `mov w5,#1; mov w4,#0x7530` vs swapped regs = argument registers only); the `(x+w5)/2` recomputation is the same expression | no |
| fg_update_status | 1119/1119 | 46 | SCREENED | all hunks: same multisets of constants, calls, strings and condition codes (the only 2 flagged hunks are stack zero-init reorders) | no |
| strategy_batt_overvoltage_dfx_work | 195/196 | 25 | SCREENED | flagged hunks: `b.gt`->`b.lt` with moved fall-through, `cmp #1;b.lt` = `cmp #0;b.le` | no |
| strategy_isc_detect_work | 200/200 | 12 | SCREENED | `cmp #1;b.hi` = `cmp #2;b.hs`, `cmp #5;b.hs` = `cmp #4;b.hi` | no |
| strategy_isc_detect_2_work | 134/133 | 8 | SCREENED | no cond-flagged hunk | no |
| strategy_fg_process_event | 391/391 | 3 | EQUAL-READ CODEGEN | the whole tail block (fake_soc handling: -1/-1 log, `0xc6c`, cancel/queue, notify 2/0x11, update 0xfffffc19) moved | no |
| strategy_fg_monitor_workfunc | 1903/1897 | 143 | COVERED-BEFORE (G25c: 24 blocks by hand) | not re-read | no |

### mca_strategy_quickchg (26 of 83) - read line by line by G22c/G25b/G30e/G31a; here only the not-covered ones touched
| function | verdict | evidence |
|---|---|---|
| mca_quick_charge_update_jeita_fcc | EQUAL-READ | whole function: diff = cur - sum; `<= a40 ? a40+100 : diff`; store sum only if `0x85c <= diff`; log values equal | 
| mca_quick_get_parallel_volt_para | EQUAL-READ (parallel only) | hys enum HIGH=1/LOW=2: stock `==2 -> LOW handler`, `==1 -> temp >= high - hys -> log 0x3a5`; ours `==1 -> HIGH handler`, `==2 -> temp <= low + hys -> log 0x3ab`: the two logs come from the two source lines 933/939, same predicates |
| mca_check_cp_work_mode, mca_cp_check_initial_mode, mca_get_cpmode_unrelated_fcc, mca_select_cp_work_mode_by_current, mca_quick_charge_select_cur_work_mode, mca_quick_charge_update_work_mode_para, mca_quick_charge_open_path, mca_quick_charge_get_vstep, update_batt_para, mca_quick_charge_msleep, mca_quick_charge_select_stage, mca_quick_charge_sysfs_store, mca_quick_charge_can_tbat_do_charge, stategy_quickchg_is_pdo_9v, mca_quick_charge_adjust_adapter_mode, mca_quick_charge_start_charging, mca_eu_pps_anti_disconnection_strategy, mca_quick_charge_ovp_monitor_work, mca_quick_charge_parse_chg_mode_info, mca_quick_charge_parse_batt_info | SCREENED + COVERED-BEFORE (G22c/G25b/G30e/G31a atoms/manual reads) | start_charging: `soc too high` log block moved + `b.ge`/`b.lt` inversion; check_cp_work_mode: log string form only; eu_pps: store order of 3 independent stores; no flagged hunk with changed constants beyond those |
| mca_quick_charge_monitor_work, mca_qc_quick_charge_regulation, mca_quick_charge_regulation, mca_quick_charge_process_event | COVERED-BEFORE | read by G30e/G31a (regulation 16 sections; monitor_work line by line after the taper fix; process_event 86-entry jump table). NOT re-read by G40e (113/66/9/11 hunks) |

## Result
No proven BEHAVIOUR difference was found in any of the 92 differing functions; therefore no source edit, no rebuild, no new md5.
Non-observable order differences noted (left as is): pwm_fan `set_pwm` (store of pwm_duty after the log; 64-bit vs 32-bit duty in the log text), jeita monitor_work (`base_over_curr_count` zeroed after the vote), update_fw_mainthread_work (NULL-init position), antiburn sysfs_show (field read before/after a call that does not write it).
STILL NOT PROVEN (ordered by risk): (1) quickchg regulation / monitor_work / process_event event bodies (only G30e/G31a reads, hand-read but the lifter was used in part of G30e); (2) fg_comp monitor_workfunc (G25c, 24 blocks by hand, not re-read), fg_update_status, batt_overvoltage_dfx_work, isc_detect_work/2 (feature screening only); (3) buckchg monitor_workfunc / class_probe / process_event / source_status / wls_revchg (partly read here, rest G22d/G25b symcmp5 + G31a); (4) antiburn probe spill-slot-to-property mapping; (5) fg_comp get_manufacturing_date (parallel only, dead on dada); (6) basic_wireless class_probe / process_event / process_irq (G31a).
Tools: work/re/g40e_tools (alpha3.py, sus2.py, sus3.py, rng.py, setup + driver scripts); server scratch <work-dir> (lst/, rep/, st/, built/, 9 MB, no bazel-out, no stub .ko).
