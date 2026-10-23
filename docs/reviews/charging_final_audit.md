# Charging final audit (goal-G31a, 20.10.2026)

COMMUNITY-ONLY, DO NOT LOAD, never flash, never in a ROM build or the kernel package. Battery-safety relevant: NOT a safety claim, nothing tested on hardware. Base = GLOBAL OS4.0.0.7 stock. Reading by an the sx.py lifter (work/re/g30e_tools), NOT a proof.

## Method
* sx.py lifts stock and our listing to block pseudo-C; normaliser (scratchpad nrm.py) drops block ids / phi numbers / stack offsets and diffs the statement sequence (calls with args, stores, IF conditions with polarity). Every hunk was then read in the block graph (targets) against the C source. Jump tables were dumped from the .ko (.rodata / .rodata.<fn>) and passed to sx.py (--jt); event -> handler mapping compared per index.
* monitor_work was read in full on the STOCK lift (742 lines) against the source of every inlined helper (ibus/ibat queue push, read_ibat_ma, judge_thermal_vote, qc and hw taper, is_charge_abnormal (qc, single, dual), select_volt_para / can_tbat, check_switch_ffc_mode, select_parallel_volt_para, check_charge_done incl. raw listing for the iterm stack slot, charge-done branch, change_path, check_cp_work_mode gate, regulation call, stop/requeue).
* Limits: no stack-slot modelling (values via pointers look like 0/stk[n]); field reads at different times are indistinguishable; statics named by offset; .rodata contents are NOT dereferenced by the lifter, tables were compared separately (jump tables above, SW-OCP table, class_probe byte table). Equal text does not prove equal timing.

## Table (function, sections read, verdict, evidence)
| function | sections | verdict | evidence |
|---|---|---|---|
| quickchg monitor_work | 18 sections: chip_ok; ibus/ibat push; third-pps read_ibat + soc>=94; thermal vote (log 1335/1337); qc is_charge_abnormal + qc taper; non-qc abnormal (dual/single/fg) + hw taper (gate, base_flip_same temp 34/33, eff_on_p, 300..400, cp_switch_pmic_th, section==size-1, -15); select_volt_para/can_tbat; check_switch_ffc_mode (-200/-500, 60 s); parallel volt para; select_stage + fastcharge static; check_charge_done (count>5, hwid 0x496); done branch (30 s pmic); change_path; cp mode gate; regulation; stop/requeue | **BEHAVIOUR fixed: qc taper**; rest EQUAL / CODEGEN | stock raw 0x58c..0x5b4: vbat<=th jumps to the soc test (timer runs on (vbat>th && ibat<fcc) || soc>thr); ours vbat>th && (ibat<fcc || soc>thr). After fix the lifted block graph equals stock (vbat<=th -> soc test). info==NULL path of the inlined queue push (dead), csel vs branch, merged taper logs, statics names = CODEGEN |
| quickchg process_event (86 entries) | table 86 -> 28 handlers + default, per-index handler signature, full body diff | EQUAL / CODEGEN | idx sets and first statements equal; only hunks: pre_charge_check and change-type partition (7..8 / 11..12 order; type 11: protocol, 12: flags+protocol, same effects), constants 5000/9500/2500 equal |
| basic_wireless process_event (84 + 8) | both tables, all handlers, reset_trans_list loop | **BEHAVIOUR fixed: reset_trans_list** | stock drains whole list under one list_lock (safe iteration), ours one entry per lock hold; now list_for_each_entry_safe. Other hunks: call names (nm-verified), 125/2500 tail-merged queue_delayed_work |
| basic_wireless process_irq (+ inlined chg_handler, 2 tables 8+8) | full body diff | **BEHAVIOUR fixed x3** | (a) FAST_CHARGE: stock `car_mounted && uuid != 0x0e0e0205`; (b) tx_q2 selection in TRANSPARENT_SUCCESS (kind flag && support_q[kind], 3 kinds incl. 30 W magnet; ours 2 kinds, no support_q); (c) TRANSPARENT_SUCCESS/FAIL logs first in stock (ours last). After fix process_irq 1328 insns = stock, mnemonic diff 176 -> 60, event 24 -> 4. 0x9010a01 tx_voltage ternary = csel (CODEGEN) |
| quickchg get_base_limit_curr (table) | .rodata+0x6348 | **BEHAVIOUR fixed (G30e review)** | stock {1000,900},{500,600},{300,350},{100,150}; ours had {300,300}; fixed, our .rodata.cst32 (32 bytes) now equals stock |
| quickchg regulation order items (G30e review sec. 6) | ibus_compensation=0 vs log 3253; last_vbat vs log 3181 | fixed (order only); ratio read 4x vs 1x NOT changed (race-only, INFERRED) | stores moved before the logs |
| quickchg parse_third_pps_power_info | whole | **BEHAVIOUR (log) fixed** | stock logs 4795 only after the non-gl parse; ours logged on all paths. symcmp5b CLEAN-POL, listing 74/74 insns |
| quickchg get_vstep | whole | log arg abs_cap: G31a claimed it fixed but did NOT make it (found by review-g31a); fixed in G43a (20.10): log 2416 prints abs_cap, listing byte-identical to stock (function no longer in the differing list), rest was CODEGEN | G43a real Kleaf build: function-bytes 25 of 83 |
| quickchg parse_chg_mode_info | div_max_curr copy | CODEGEN | memcpy form, same two moves |
| quickchg check_cp_work_mode | whole | CODEGEN | two logs 3125/3136 merged by phi, call names |
| quickchg start_charging | whole | CODEGEN | soc>=91 branch targets swapped, names |
| basic_wireless class_probe | whole + 11-byte table | CODEGEN | table 0,3,..,30 identical, 11 targets 12 bytes apart both |
| buckchg process_event | 86-entry table, 31 handlers | CODEGEN | 8 callee names verified with nm, groups equal |
| buckchg get_status | both tables | CODEGEN | table bytes identical |
| buckchg class_probe | whole | CODEGEN | term_curr a1 = (hwid==NULL) ? 1 : ((hwid&~1)!=4) equal; votable names/limits equal |
| buckchg set_vterm | whole | CODEGEN-LIKELY | constants 45/40/30, (temp-48)<=6, 34/33 flag equal; temp variable timing not provable |
| fg_comp check_batt_emerg | whole | **BEHAVIOUR (order) fixed** | stock stores status=0 before the log; now equal |
| fg_comp check_li_plating | whole | CODEGEN | smull vs umull of a log argument, temp>=150 on that path |
| fg_comp overvoltage_dfx_work | whole | CODEGEN | exclusive branches only laid out differently, statics names |
| fg_comp fg_update_status | whole (139 diff lines) | CODEGEN | statics names, stack offsets, OR-chain regrouping |
| fg_comp shutdown_parse_dt | whole | CODEGEN (switch table of 5 not dumped) | 5 logs merged by one phi call (all 5 strings/lines present) |
| fg_comp get_first_usage_date / get_manufacturing_date | loops | CODEGEN-LIKELY | lifter diff only (polarity / csel), not hand-proven |

## Rebuild (real Kleaf, scratch <work-dir> bazel-out removed)
| module | function-bytes differing (before -> after) | kCFI / objects / imports | source md5 | .ko md5 |
|---|---|---|---|---|
| mca_strategy_quickchg | 27 -> 26 of 83 | 0 / 40 PASS / 119+CRC PASS | 1d065cf977400043970ae43619dee1b9 | 5d356a07b27494eff0ddba68b98a852e |
| mca_basic_wireless | 9 -> 9 of 73 (process_irq now 1328 insns = stock) | 0 / 20 PASS / 141+CRC PASS | 18f079059186d2a23f75576e50a2566b | 52b4524fe7ab729c73a637ad5628e87e |
| mca_strategy_fg_comp | 19 of 99 (G25b record 21 of 97, builds not strictly comparable) | 0 / 76 PASS / 162+CRC PASS | 983b9dc475ef336116980dce004ff0df | 324f523defc0729e3efdbddcdf7436b1 |

anon-data / other-tables FAIL (quickchg 2 chunks, wireless 3+2 chunks, .altinstructions) = jump-table and tables layout only (checked by index mapping). buildmeta/depends FAIL = forged, stub providers; .ko never shipped. symcmp5b: monitor_work still 14 polarity diffs (lifter/BDD blind spot on the unrolled tail merges, checked by hand above), regulation INCONCLUSIVE.

## Not done / open
* ratio [0x7fc] read once vs four times (regulation) not changed; race-only.
* fg_comp shutdown_parse_dt switch table, first_usage/manufacturing_date only lifter-diff.
* buckchg set_vterm temperature timing; basic_wireless monitor_work, sysfs_store, adapter_handle, get_adapter_charger_mode (differing functions, outside this task) not read.
* No independent review of the lifter or of these seven fixes yet.
