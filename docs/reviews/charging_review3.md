# Third independent review: charging modules of group G2m vs HyperOS 4.0 (OS4) stock

Reviewer: review-charging-opus3 (independent; did not write this code; earlier reviews, STATUS.md and RESUME claims were NOT trusted, everything below was re-derived from the stock disassembly). Date: 18.10.2026, ~17:00-18:00 local.
Scope (`work/a17/goal_groups/G2m.txt`): mca_strategy_fg_comp, mca_connector_antiburn, mca_wireless_revchg, mca_business_charger_comp, mca_pwm_fan, mca_buckchg_jeita.
Nothing under `work/re/src` was edited; no Bazel build, no git, no phone. Server scratch: `<work-dir>` only (11 MB, no bazel-out). Charging modules stay community-only: never flash, never in a ROM, never in the kernel package.

## 1. Summary

| module | identical / total | verdict (current sources, snapshot `charging_review3_snapshot_md5.txt`) |
|---|---|---|
| mca_buckchg_jeita | 13 / 17 | **NO BEHAVIOURAL DIFFERENCE FOUND**; 1 LOW race-only nit (R3-2). J-1, J-2, J-3 confirmed fixed |
| mca_strategy_fg_comp | 79 / 97 | **NO BEHAVIOURAL DIFFERENCE FOUND** in the 18 differing functions; FG-7 (trap bounds, unreachable) still present (R3-1); type nits (R3-3). F1..F10, FG-1..FG-6 confirmed fixed |
| mca_connector_antiburn | 12 / 17 | **NO BEHAVIOURAL DIFFERENCE FOUND**. A-1 fixed; A-2 now behaves like stock (no NULL check, as stock) |
| mca_wireless_revchg | 43 / 45 | **NO BEHAVIOURAL DIFFERENCE FOUND**. W1, W2, R-1, R-2 confirmed fixed |
| mca_business_charger_comp | 41 / 45 | **NO BEHAVIOURAL DIFFERENCE FOUND** (scheduling / register / stack only). B1 confirmed fixed |
| mca_pwm_fan | 16 / 20 | **NO BEHAVIOURAL DIFFERENCE FOUND**. P1 (kCFI) and P-1 (modinfo alias) confirmed fixed |

No finding of this review is above LOW. No extra or missing write to a charger / fuel-gauge / adapter / GPIO / voter was found in any of the 37 differing functions; all byte-identical functions are identical including resolved relocations.
None of the six is STRICT-EQUIVALENT: `buildmeta` (no `intree=Y` -> out-of-tree taint) and `depends=` (forged `os4_*` providers, import CRCs do not prove prototypes) fail in all six, as before. This is the reason (together with "no hardware test") why the standing rule "never flash" must stay; it is not a code difference.

## 2. Method

1. **Reproduction.** The current sources (md5 list in `charging_review3_snapshot_md5.txt`) were copied to `<work-dir>` and compiled with the ThinLTO-emulation fast loop (copy of the review2 `cl.sh`/`cc_rv.sh`, stubs per BUILD.bazel: fg_comp `stubs_g2k`, antiburn `stubs_g2i`, revchg and pwm_fan `stubs_a17B`, business `stubs_g5`, jeita `stubs_g2j`). Result: every function of all six modules is byte- and relocation-identical to the newest Kleaf `.ko` (`<work-dir>` for five modules, `<work-dir>` for business, whose source is unchanged since 00:40) except `init_module` (`__this_module` link artefact). So the reviewed `.ko` are exactly what the current sources produce. Stock: `<work-dir>` (md5 = review2 stock).
2. `compare_ko_v2.py -v` stock vs Kleaf `.ko` (`rep/v2_*.txt`): function set, binding, kCFI preamble words, objects (content + relocs incl. statics), anon data, bug/jump/alt tables, exports+CRC, imports+CRC, modinfo: PASS in all six except: function bytes (37 functions), anon-data (antiburn 1, business 2 jump tables: see 4.6), buildmeta, depends. `preamble_cmp.py`: 0 mismatches (75/15/42/37/14/11 functions). Trap census (`brk`/`udf` per function): identical counts in all six modules.
3. **New: symbolic comparison** (`work/a17/review/tools3/symcmp.py`). Per function and per build: CFG from the listing (relocations resolved by `compare_ko_v2.Elf`, statics by name); forward data flow with symbolic values (arguments `a0..a7`, loads `ldr.w(a0+0xb94)`, constants, call results `ret(fn,args)`, ops); stack slots tracked so spills/fills and stack layout disappear; merges become `phi{...}`. Facts compared between builds: every call with its arguments (arity estimated from both builds), every store to non-stack memory (width, address, value), stack data passed by pointer, every compare in canonical form (operand order sorted, `#N gt` = `#N+1 ge`, cbz/tbz/tst folded, signed vs unsigned kept), traps, return value. On top: **must-guards** (set of branch literals that hold on every path to the fact; per fact the intersection over its copies, so tail duplication does not matter). This sees operand swaps, signedness, wrong constants, wrong data sources and changed branch targets that register-masked multisets (semfp) and alignment (ldiff3) can miss. Self-checks: byte-identical functions give 0 differences; `pwm_fan_probe` 0; the known FG-7 bound change (`cmp #5` vs `#6`) and the stock/ours `ccmp/csinc` vs branch shape in jeita swocp are reported.
4. `ldiff3.py -S` (register/stack-masked alignment + branch-target mapping) on all 37 functions (`rep/ld_*.txt`); every branch mismatch and every remaining symbolic difference was read by hand in the side-by-side listings (`lst/S_*.lst`, `lst/O_*.lst`) and against the C source.
5. Jump tables (`tools3/jtmap.py`): every entry of the antiburn `sysfs_show` (6), business `usb_psy_get_prop` (62) and `event_thread` (84) tables resolved to its target block in both builds; targets are the equivalent blocks (differences only in register numbers, stack offsets and branch numbering).

Limits of the symbolic tool (documented so nobody over-trusts it): a value built in a branch diamond without side effects in the arms becomes an unordered `phi{a|b}` (a swapped diamond would not show in the facts; ldiff3 alignment covers that case and was read for the safety-critical blocks); stack out-parameters are labelled with the last call, not the real writer (out-parameter mapping was checked by hand where it mattered, e.g. li_plating temp/volt/curr); jump-table case values are not modelled (point 5 covers them).

## 3. Findings (ranked by battery-safety impact; none above LOW)

**R3-1 (LOW, no runtime effect; = FG-7 of review2, not fixed) array-bounds trap constants differ from stock.**
`isc_do_point_sample_2`: stock +0xc8 bounds check of the inner index `(sample_index % 5) < 5` -> `brk #0x5512`, ours +0xc8 `< 6`. `strategy_li_plating_work`: stock +0x240 `ldr w8,[x19,#0x9cc]; cmp w8,#0xa; b.hs @524 (brk)`, ours +0x238 `cmp w8,#0xb; b.hs @51c`. The arrays in our headers are declared one element larger than stock. Reachability: `[x19,#0x9cc]` (x19 = the work argument) is only written with `count % 10` (stock +0x140 `str w3,[x19,#0x9cc]`, w3 from `msub`), and the ISC index is `% 5`, so both traps are unreachable for non-negative counters; structure offsets are identical (all loads/stores at the same offsets). Effect if a counter ever went negative/corrupt: stock traps one index earlier than ours. Fix (cosmetic): declare the arrays with the stock sizes (5 and 10).

**R3-2 (LOW, race only) `mca_strategy_buckchg_jeita_get_status` (high-temperature range check, status 0x13) reads `cur_jeita_index` twice.** Stock +0xec `ldr w21,[x20,#0xf0]` once, logs it (+0x110) and compares the same register (+0x114/+0x120 `tbnz w21,#31` / `cmp w21,w8`). Ours +0xf8 loads it for the log and +0x110 `ldr w8,[x20,#0xf0]` again for the compare (`mca_buckchg_jeita.c:1326-1329`). Only a concurrent `monitor_work` update between the two loads can make the logged and the compared value differ; the result is then computed from the newer index. No effect on thresholds. (Already noted by review2; still present.)

**R3-3 (LOW, no observable effect) signedness of five 32-bit counters differs from stock.** Stock `ldrsw` vs ours `ldr w` (fields declared unsigned in our headers) for `[x0+0x6a0]` in `update_vcoutoff_v2_work` +0x50 (x0 = work = fg+0x4d0, i.e. fg+0xb70, the same field `check_li_plating` reads at `[fg+0xb70]` for `< 150` / `< 160`), `[x0+0x818]` in `isc_detect_work`, `[x0+0x8f0]` in `isc_detect_2_work`, and the opposite (`ldr w` stock, `ldrsw` ours) for `[x0+0xf58]` in `isc_do_point_sample_2`. Every use is a 32-bit compare (same signedness of the condition as stock), a W-register `smull` division, or a `/5`, `%5`, `/10` after a `>= 1` guard, so all results are identical (checked symbolically: the only differing facts are the load mnemonics). Fix (cosmetic): use `int` where stock sign-extends.

Not findings (checked, equivalent; listed because a tool flags them):
- jeita `monitor_work` swocp block: stock `ccmp/csinc` (+0xfdc/+0xfe0) vs ours branches (+0xfb4..+0x1040): `count = (eff != 0 && now/1000 < -chg) ? count+1 : 0`; vote `swocp` with `eff-100` when `count >= 4 && eff != 0` (ours tests `eff` before the increment, same set of paths), release when `now/1000 > 500-chg && runswocp`: equal. Index store `stock tbnz w23 / tbnz w28` vs ours `and w28,w25; cmp #1`: same predicate `!hys_affect && data_change` (base update unchanged since review2's line-by-line read).
- jeita probe: `weak_chg_vterm_cfg` override buffer is zeroed at the start of the inlined override (ours +0x4d0) instead of right before the call (stock +0x594); nothing writes the slot in between: equal.
- business `report_quick_charge_type_work`: max(stored, adapter power) only when the PD flag `+0x1f9` is set, raw power otherwise, thresholds `>49 -> 4, >29 -> 3, >19 -> 2, else 1`, PPS path `>29 ? 3 : 1`: equal (stock stores in two places, ours in one).
- antiburn `get_temperature`: ours stores `adc_err_flag` (already tested `== 1`) into `flag_restore`, stock stores `#1`: equal. `monitor_workfunc` `flag_restore` tested with `cmp #1` vs `tbz #0` (bool, only 0/1 stored), `t1 >= 55000` vs `> 54999`: equal.
- fg `monitor_workfunc`: `fg_update_status(fg, !(resume || boot))` (stock via branches +0x158/+0x28c, ours `cset` +0x15c); QBG `chg_data.quick_chg_en` stored before (ours +0x1b4) instead of after (stock +0x2cc) the voter reads, same value; `check_soc_smooth` `delta > 29` and the three flag stores per branch: equal; `get_term_current_new` re-reads a static and `fg+0x930/0x940` after a log call (single work context): equal.
- fg `get_manufacturing_date` / `get_first_usage_date`: both empty -> 0 / "99999999", one empty -> the other one, both present -> byte compare (manufacturing: bytes 3..7, earlier date; first usage: bytes 0..7, later date), equal -> master: equal (read by hand).
- fg `strategy_batt_overvoltage_dfx_work`: uevent struct `{buf, len=32}` and the 32-byte `CHARGE_DFX_BATTERY_OVERVOLTAGE=1/0` copy at another stack offset; `vbat > 4199` vs `>= 4200`: equal.
- fg `strategy_update_vcoutoff_v2_work`: average `(vsiloss + x)/2` recomputed from the stack after a log call instead of kept in a register: equal.
- pwm_fan `set_pwm`: success paths return 0 where stock returns `pwm_reg_config()` (= `pwm_apply_state()`, 0 on success); the "exit: new duty" log/stored duty is 0 on the power-off path in both: equal. `set_pwn_fan_target_level` / `fan_sysfs_store`: `bit0((enabled!=0) ^ (level==0)) == 0` vs `(enabled!=0) != (level!=0)`: same truth table.

## 4. Earlier findings: status in the CURRENT sources (re-verified, not taken from STATUS/RESUME)

| id (review) | status | evidence (this review) |
|---|---|---|
| F1 qbg_send_chg_data prototype (1) | fixed | monitor stock +0x2dc..+0x2e4 / ours +0x2c4..+0x2cc `add x1,x19,#0xd70; mov w0,wzr; bl platform_fg_ops_qbg_send_chg_data` after `get_status(7,0x13)` == 0; struct fill 1/5, 1/0x19, 1/0x1a, voters, defaults 500/565/4480 identical; symbolic facts equal |
| F2 new SoC smooth (1) | fixed | `fg_ui_soc_smooth` 0 symbolic differences; 7 statics of `fg_update_mapped_soc_new` present (objects PASS) |
| F3 / FG-3 DOD table for vcutoff v2 (1,2) | fixed | `mca_battery_shutdown_parse_dt`: facts and guards equal; only difference = our five error logs share one call site (lines 172/200/215/275/289, same return) |
| F4 update_vcutoff_para (1) | fixed | byte-identical |
| F5 para_iterm (1) | fixed | monitor facts equal (`last_target_iterm` vs `iterm`, vote `iterm+40`) |
| F6 DEVICE_POWER_MAX 23, memset, cold zone (1) | fixed | monitor facts equal |
| F7 / FG-4 set_charging_done (1,2) | fixed | byte-identical |
| F8 force_fw_report_full (1) | fixed | 0 symbolic differences (`iterm+30`, `vterm-23`, cold_zone -> 3) |
| F9 probe / parse_dt_override (1) | fixed | `strategy_fg_probe` byte-identical |
| F10 manufacturing / first usage date (1) | fixed | read by hand, see section 3 |
| F11 / FG-7 trap bounds (1,2) | **open** (no effect) | R3-1 |
| FG-1 charging_done clear on unplug (2) | fixed | `strategy_fg_process_event` 0 symbolic differences (guards included) |
| FG-2 parallel termination early return (2) | fixed | monitor facts/guards equal |
| FG-5 isc_do_point_sample_2 fall-through (2) | fixed | 0 branch mismatches; only R3-1/R3-3 differences |
| FG-6 report_li_plating_data len check (2) | fixed | `strategy_li_plating_work` / `report_isc_data_2`: 0 branch mismatches; only loop-induction shape and R3-1 |
| W1, W2 fw update end / state 2 (1) | fixed | `update_fw_mainthread_work` 0 symbolic differences |
| R-1 TX-only hboost, R-2 close path (2) | fixed | `rev_charge_config`, `enable_reverse_charge` byte-identical |
| B1 batt-health else branch (1) | fixed | `event_thread` 0 symbolic differences except a phi artefact on the `fsa4480_switch_event(node, sbu)` node register; jump table targets equivalent |
| P1 kCFI pwm_reg_config (1), P-1 modinfo alias (2) | fixed | preamble 0 diffs; modinfo PASS |
| J-1 smart-charge type 4 (2) | fixed | objects PASS (`g_jeita_smartchg_if_ops` first word 4, relocs +0x10/+0x48) |
| J-2 flip vote under hysteresis (2) | fixed | flip-update facts and guards equal (vote on every data_change, index store only when !hys_affect) |
| J-3 parse_para -1 (2) | fixed | byte-identical |
| A-1 mos_ctrl log (2) | fixed | `antiburn_sysfs_store` facts equal (GPIO untouched for val > 1, log present) |
| A-2 is_triggered NULL check (2) | stock behaviour | byte-identical to stock: dereferences `g_conn` unconditionally, as stock (oops if called before a successful probe, also in stock) |

## 5. Verified equal (this review)

- **All byte-identical functions** (79 + 12 + 43 + 41 + 16 + 13): bytes + resolved relocations equal (compare_ko_v2), kCFI words equal.
- **mca_buckchg_jeita**: `monitor_work` (JEITA tables search, hysteresis, extreme-cold, vbat thresholds l->s / s->l, weak charger, vfloat > 4150 jeita-hot, dtpt, en/fcc/vterm/iterm votes, base swocp, flip vote), `probe`, `parse_dynamic_iterm_by_name` (stack layout only), `get_status` (R3-2 only). objects incl. `g_jeita_smartchg_if_ops`.
- **mca_strategy_fg_comp**: `monitor_workfunc`, `process_event`, `panel_notifier_cb` (two pure checks reordered), `fg_update_status` (mievent buffers: same values, other stack offsets), `parallel_force_fw_report_full`, `fg_ui_soc_smooth`, `mca_battery_shutdown_parse_dt`, `update_vcoutoff_v2_work`, `isc_detect(_2)_work`, `report_isc_data(_2)`, `isc_do_point_sample_2` (R3-1/3), `check_li_plating` (R3-3), `li_plating_work` (R3-1; temp/volt/curr out-parameter slots mapped by hand: stock curr@0x24 volt@0x20 temp@0x1c, ours curr@0x20 volt@0x24 temp@0x1c, stored to +0x9e0.. / +0xb20.. identically), `batt_overvoltage_dfx_work`, manufacturing / first-usage date.
- **mca_connector_antiburn**: `monitor_workfunc` (973 insns, 0 branch mismatches), `probe`, `get_temperature`, `sysfs_show` (jump table equivalent), `sysfs_store`.
- **mca_wireless_revchg**: `rev_charge_config_work`, `update_fw_mainthread_work` (0 symbolic differences).
- **mca_business_charger_comp**: `probe`, `event_thread` (84-entry table equivalent), `usb_psy_get_prop` (62-entry table equivalent; zero-init width of out-params only), `report_quick_charge_type_work`.
- **mca_pwm_fan**: `set_pwm`, `set_pwn_fan_target_level`, `fan_sysfs_store`, `speed_detect_req_work_func`.
- Tables/data: objects (named + statics) PASS everywhere; anon data PASS except the three jump tables above (equivalent targets); bug/jump/alt/ex tables PASS; imports + CRC PASS; exports + CRC PASS (antiburn 1, revchg 15); modinfo PASS (incl. pwm_fan alias); kCFI preambles 0 diffs; trap counts per function equal.

## 6. Not checked / caveats

- No hardware, no Kleaf build of my own (fast-loop rebuild == agents' Kleaf `.ko`, function for function). DT facts (which paths are live on dada) are taken from review2's dtbo parse, not re-parsed.
- The symbolic tool and ldiff3 are strong but not proofs (limits in section 2); in `monitor_workfunc` (2032 insns) and `fg_update_status` (1169) not every aligned instruction was read by hand, only every difference the tools reported.
- Not in scope: the other users of the smart-charge enum (quick_wireless, strategy_quickchg, basic_wireless, smart_charge: still BLOCKED), modules outside G2m, ROM trees and `<work-dir>` (not touched).
- `depends=` (forged `os4_*` names) and missing `intree=Y` make these `.ko` non-drop-in even if the code is equal; they must not be loaded on a battery device.
- Server artefacts: `<work-dir>` (compare_ko_v2 -v, symcmp, ldiff3 per function). Tools: `work/a17/review/tools3/` (`symcmp.py`, `jtmap.py`, `rng.py`).
