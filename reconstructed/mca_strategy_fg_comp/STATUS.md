# mca_strategy_fg_comp - PARTIAL vs the OS4 stock: 74 of 97 functions identical (G2d, 17.10.2026; was 48 of 97 at the start of G2d)

SAFETY: charging / fuel-gauge strategy module: community-only source, never flash it, never put it into a ROM build (the ROM uses the stock module).

## G2e (17.10.2026): not worked on (budget went to nuvolta_1652 / mca_wireless_revchg / mca_pwm_fan); state unchanged, still 74 of 97 identical, does not link
Notes for the next attempt: use the G2e loop (`work/re/g2e_tools/sm.sh`, `fgq.sh`, `ndiff.py`; the sm.sh `ok` flag only compares the `__LINE__` immediates, a function with worddiff>0 and reldiff=0 is a REAL difference: look at it with `fgq.sh <mod> <func>`), declare locals in the stock frame order (it decided 16 bytes of frame in mca_wireless_revchg), check `imports` early: the real Kleaf compare (`bin/rb.sh`) lists imports that the source never calls (they are the new OS4 calls: the 10 missing ISC / li-plating / vcutoff-v2 functions will bring the missing imports).

## G2d (17.10.2026): rebuilt against the OS4 stock (97 functions, 4 translation units)
Loop: `INC=stubs_g2d work/re/g2d_tools/sm.sh mca_strategy_fg_comp [maxwords]` (multi-TU ThinLTO emulation vs `<work-dir>`, compact summary; `work/re/g2d_tools/fgq.sh <mod> <func..>` prints the diff of single functions; `fg/pin.sh` re-pins every `__LINE__` with pb_pin2 from `g2d_tools/dump_mca_strategy_fg_comp`; patch scripts `g2d_tools/fg/p*.py` use `g2d_tools/ed.py`). Server scratch `<work-dir>`, include overlay `<build-dir>` (copy of stubs_mcaB + OS4 additions, saved in `g2d_tools/stubs_g2d/`).
Status: 74 functions identical (bytes + relocations + __LINE__), 13 differ, 10 are still missing. NOT built with Bazel / `compare_ko.py` yet (the new functions are missing, so the module cannot link; exports/imports/CRCs not checked; new platform ops (`platform_fg_ops_get_fast_mode_times/set_fast_mode_times/get_vsiloss/get_iaver/get_cc_coulomb_integral/get_batt_qmax`) and the ops struct additions exist only in the stubs_g2d overlay).
* `struct strategy_fg` (5576 B), `strategy_fg_cfg` (632), `mca_battery_shutdown_para_info` (40), new structs `fg_chg_data, batt_ov_data, isc_detect_info, batterydata, li_plating_info`, enums `isc_state/li_plating_state` rebuilt from the OS4 BTF (`START_ID=141904 python3 -I btf_dump.py local <ko>`, dump in `g2d_tools/fg/btf.txt`), with `_Static_assert` on sizes/offsets. This alone made 16 functions byte-identical once `__LINE__` was pinned (the OS3 pins were stale).
* New 4th TU `mca_battery_safe.c` (log tag `[mca_battery_safe]`: ISC detection, li plating, over-voltage dfx): done `stop_isc_detect_work`, `stop_isc_detect_2_work`, `stop_li_plating_detect_work`, `mca_strategy_start_isc_detect_2_work` (identical).
* New/changed in `mca_strategy_fg.c`, all identical to the stock: sysfs attrs `i_aver_gear`, `siloss_gear` (table 26 entries), `strategy_fg_get_status` codes (0x15/0x18), `onewire_*` stubs (check_* now `false`, `get_shutdown_uisoc` -> *uisoc=15, -1; `set_shutdown_uisoc` -> -1), `is_chip_ok` (`fg_init_flag || onewire_init_flag`), `ops_get_comp_fcc/get_comp_rm/set_allow_new_soc_smooth` (+ 3 new members of `struct strategy_fg_class_ops` in the stubs_g2d header), `mca_strategy_fg_shutdown_cb` (register_reboot_notifier, priority 255, support_qbg / fast-mode-times), `cfg.support_fast_mode_when_charging` in `ops_get_curr/volt/fastcharge` and the update helpers, `panel_notifier_cb` (isc2 need_stop, 2 words differ: stock tests `support_isc_li_plating` before `status`, 8 source shapes tried), `start_recharging` (isc stop), `force_report_full_work` (`batt_cyclecount < cfg.drop_fv_cycle`), `dtpt_monitor_work` (isc alert mievent switch 0x30..0x32), `get_dod_count` (`support_dod_vcutoff_v2`), `record_volt_mean` (mievent 0x35 on vbatt_empty), `batt_abnormal_dfx_work` (stock info struct is 72 bytes: `fg_batt_abnormal_info` +8 ints; an ARRAY member changes the stack layout (stack-protector classification), use separate ints), `strategy_fg_parse_extreme_cold_dt` is `noinline` (out of line in the stock; documented hack).
* Still different (all real OS4 changes, not offsets): `monitor_workfunc` 2009 words, `probe` 1230 (reduced OS3 shape, stock registers more works/cfg/parse_dt calls), `fg_update_status` 923, `fg_ui_soc_smooth` 794, `battery_shutdown_parse_dt` 409 (new siloss/i_aver gear DT), `process_event` 338, `update_vcutoff_para` 301, `parallel_force_fw_report_full` 219, `ops_set_charging_done` 201 (new isc / li-plating start/stop logic after charging done: isc_detect_work queue 0x493e0 jiffies, li_plating full_count / charge time < 60 s ...), `check_shutdown_delay` 159, `get_manufacturing_date` 61, `get_first_usage_date` 44 (known memcmp issue above), `panel_notifier_cb` 2.
* Missing (10), all in the battery-safe / vcutoff-v2 family: `report_isc_data` (508 B), `report_isc_data_2` (768), `isc_do_point_sample_2` (1140), `strategy_isc_detect_work` (880), `strategy_isc_detect_2_work` (600), `strategy_li_plating_work` (1324), `mca_strategy_check_li_plating` (404), `strategy_batt_overvoltage_dfx_work` (796, global, `strategy_fg_*` TU), `strategy_update_vcoutoff_v2_work` (1528, in the battery_shutdown TU), `mca_battery_shutdown_update_cc_para` (544). Stock log strings with their `__LINE__` (li_plating lines 458/497/501 ...) are in the pinner output (`fg/pin.sh`, "UNUSED stock" lines).
* Pitfalls met: `lst.py` appends ALL later relocation names to the last line (always cut output); heredocs halve backslashes (use `ed.py`'s `@BS@`); `pb_pin2` inserts `#line` between statements, so patch scripts must not include log lines in their match text.


Real Kleaf build + `compare_ko.py` (stubs in `re/stubs_mcaB`, method of the other ThinLTO modules):

```
OK   function set - 76 functions
DIFF function bytes - differ: strategy_fg_monitor_workfunc, mca_strategy_get_manufacturing_date,
                      mca_strategy_get_first_usage_date
OK   function relocations (resolved targets) - all identical
OK   global objects (name,size)
OK   exported symbols + CRCs - 0 symbols
OK   imported symbols + CRCs (__versions) - 133 symbols
OK   modinfo (license/author/description/alias)                   (two description sets: strategy_fg + soc_smooth)
```
Everything else (data, imports, exports, modinfo, the .rodata string set, the init/exit functions, `onewire_check_*`, the soc_smooth and
battery_shutdown translation units) is identical.  No compiler flag or source hack is needed for the 73 identical functions.

## Layout of the sources

Three translation units exactly like the stock Kbuild (`strategy_fg.o soc_smooth.o battery_shutdown.o`, ThinLTO `ld.lld -r`):

| file | content |
|------|---------|
| `mca_strategy_fg.c` | main TU (probe, monitor work, event handler, sysfs, ops, onewire stubs, init/exit) |
| `mca_soc_smooth.c` | `strategy_soc_smooth_parse_dt`, `fg_ui_soc_smooth` (+ inlined helpers) - IDENTICAL |
| `mca_battery_shutdown.c` | `mca_battery_shutdown_parse_dt/parse_vcutoff_para/update_vcutoff_para/check_shutdown_delay` - IDENTICAL |
| `strategy_fg.h`, `mca_battery_shutdown.h` | struct layouts from the BTF of the stock module (`_Static_assert` on sizes/offsets) |
| `BUILD.bazel`, `mca_strategy_fg_comp_defconfig` | `CONFIG_LTO_CLANG_THIN=y` per module, `-ffunction-sections -fdata-sections` |

`#line` directives reproduce the `__LINE__` values baked into the stock log strings (for a multi-line macro call `__LINE__` is the
line of the closing parenthesis).

## Remaining differences

All three are instruction-order / block-layout differences (same instruction multiset, same call targets and strings).

1. `strategy_fg_monitor_workfunc` (6348 B; 1587 insns stock / 1580 rebuilt).  All logic is reconstructed (init handling, qbg, soc-not-full
   dfx, termination single/parallel with inlined first-termination / get_term_current_new, force-fw-report-full, recharge, lossless
   recharge, fl4p0, charge-slowly dfx, onewire auth, interval adjust); the event order of strings/calls matches to 244/244 events, only
   block placement differs: stock keeps the `strategy_fg_check_parallel_termination` tail and the hwid/fl4p0-enable block at the very end
   of the function and the `mca_strategy_check_sigle_termination` call right behind the `fg_type` dispatch, and uses two more callee-saved
   registers (x27/x28); rebuilt places the parallel chain inline.  `scripts_mcaB/outline.py` shows the moved runs.
2. `mca_strategy_get_manufacturing_date` : same compare chain, but stock materialises `master_empty`/`slave_empty` as booleans, shares one
   final `ldr x8,[sp+8+8*idx]` tail for all "copy one date" cases and prints `choose_slave & 1`; ~30 variants of the empty/compare/copy
   structure tried (best = 28 differing disassembly lines).
3. `mca_strategy_get_first_usage_date` : in stock the two `memcmp(date, "99999999", 8)` calls survive to the code generator
   (`ExpandMemCmp` creates the 64-bit loads late, the literal stays in `.rodata`, the byte loads of the compare chain are not forwarded);
   with every spelling tried the middle end folds them early (after the always-inliner the fortify `memcmp` wrapper becomes
   `__builtin_memcmp` on an 8-aligned alloca).  `strcmp` gets close but adds a call the stock does not have, so it was NOT used.

## Reusable findings (all verified on this module)

* `goto` bypass switches the lifetime markers off: clang omits `llvm.lifetime.start/end` for locals declared after a `goto` that jumps over
  them (C: also after any earlier label in the scope).  `strategy_fg_process_event` needed `int ret = 0; if (!data) { ret = -1; goto out; }
  ... out: return ret;` with `ota_flag` declared right after the check - without markers the exit blocks are empty, the plug-out tail
  returns directly and the jump table becomes a byte table.  Nothing else (case order, return forms, `unlikely`) changed the layout.
* Structure of the XRING sources is the best guide even for the Qualcomm parts: `strategy_fg_check_parallel_termination()` ends with the call
  to `mca_strategy_check_sigle_termination()` (not `check_termination()`), which makes the single-termination function stay out of line
  naturally - an earlier `noinline` pin was wrong and is gone; `mca_strategy_check_fl4p0_status` starts with
  `if (!fg || charging_done || recharging || !voter_ok || !fast_charge || ffc_continue_charge) return;` (after the calibration vote);
  `ffc_safe_item` is a 2-D array with the cycle/temp chains in `mca_strategy_get_ffc_safe_item()`.
* `-Rpass=inline -Rpass-missed=inline` on the compile line (`cc.sh ... -flto=thin -Rpass=inline`) explains inlining differences
  (e.g. two call sites at the time the inliner runs = no last-call bonus).
* `ld.lld -mllvm -print-before=block-placement -mllvm -filter-print-funcs=<f>` (see `scripts_mcaB/dumpmir.sh`) prints the MIR with branch
  probabilities; `-print-before=codegenprepare` prints the final IR; `-print-after-all` on the compile shows which pass changes what.
  MachineBlockPlacement only lets a block follow a chain when all its predecessors are placed, so an extra predecessor
  (e.g. an exit block that holds `lifetime.end`) moves whole tails.
* `strategy_fg_get_soh_new`: three separate statements (`if (m bad) m = 0; if (s bad) s = 0; if (!m && !s) {defaults}`) instead of the
  nested if/else-if; the nested form made LLVM fold the divided range checks differently.
* `switch (country_version)` with `default` needs an explicit `case 1:` next to it when the stock has a dead `cmp w8,#1`.
* Condition order in `if (!a && !b && !idx)` is reordered by LLVM (cheap register compare moved); the source order that reproduces the
  stock asm may differ from the asm order (`!fg1 && !fg2 && !fg_index` gave `fg1, fg_index, fg2`).
* A scheduler-visible difference in zero-store placement (`-ftrivial-auto-var-init=zero`) follows the declaration position of the variable.
* Stock vcutoff code differs from XRING: `cyclecount <= range_upper`, `dod_count < range_upper`, rollback needs `!disable_rollback_shutdown_volt`,
  `vcutoff_sw` depends on `cfg.support_base_flip`, heavy-load and vpack-low are inlined, the vpack threshold is the constant 3000.
* A log call can carry more arguments than the format has (stock "start equal report full" passes `fg_index` and the count to one `%d`).

## Tools (work/re/scripts_mcaB)

`itm.sh <module> [func...]` multi-TU ThinLTO fast loop with per-function diff (use `UPSTUBS=1` after changing the stubs),
`variantsm.py` tries source variants of one region (generators `gen_*variants*.py`), `push.sh <module>` real bazel build + compare,
`dumpmir.sh` MIR/IR dumps, `outline.py` event-order comparison stock vs rebuilt, `lifetest.py` label (no lifetime markers) experiment.

G2 note (17.10.2026, no code change): re-read the stock `mca_strategy_get_first_usage_date`: the literal "99999999" is NOT kept in .rodata in the OS4 binary: it is built in a register (`mov x20,#0x3838383838383838; orr x20,x20,#0x1111111111111111` = 0x3939393939393939) and compared with two 8-byte loads (`ldr x8,[sp]`, `ldur x9,[sp,#9]`: the two date buffers are 9 bytes each, contiguous, 18 zeroed bytes: `stp xzr,xzr,[sp]; strh wzr,[sp,#0x10]`), with the two booleans materialised by `cset ne`; the unrolled compare loop and the `x8 = idx*9` index select (`mov w8,#9`/`xzr`, `ldr x20,[x9,x8]`) follow. In `mca_strategy_get_manufacturing_date` the stock prints `choose_slave & 1` and tests it with `tbz w21,#0`, i.e. `choose_slave` reaches the code as an un-normalised value (not a plain `bool` cset): worth trying `unsigned int/u8 choose_slave` computed as `a >= b` through a helper or `choose_slave |= ...` instead of more `if` shapes. Lesson from mca_bmd (see its STATUS): a bool OUT-PARAMETER round trip can invert a phi (InstCombine folds `icmp eq (zext x), 0`), changing polarity and block layout of a whole function; the same trick (move the consumer into the callee) is not applicable here but check any `bool *`/`int *` out-parameters that were introduced to avoid inlining.


## G2b note (17.10.2026) - IMPORTANT: the 73/76 figure is against the HyperOS 3 binary

Everything above was measured against `<build-dir>` (OS3, 170240 B, 76 functions). The OS4 stock
(`<work-dir>`, 261360 B, with BTF) has **97 functions**: 21 are new in OS4
(`isc_do_point_sample_2, report_isc_data, report_isc_data_2, strategy_isc_detect_work, strategy_isc_detect_2_work,
mca_strategy_start_isc_detect_2_work, stop_isc_detect_work, stop_isc_detect_2_work, strategy_li_plating_work, mca_strategy_check_li_plating,
stop_li_plating_detect_work, strategy_batt_overvoltage_dfx_work, strategy_update_vcoutoff_v2_work, strategy_fg_parse_extreme_cold_dt,
mca_strategy_fg_shutdown_cb, onewire_get/set_shutdown_uisoc, mca_battery_shutdown_update_cc_para, strategy_fg_ops_get_comp_fcc/get_comp_rm/
set_allow_new_soc_smooth`) and almost every old function differs (struct strategy_fg grew: e.g. `fg_init_flag` +0xaf0 in OS4 vs +0x830 in the
rebuilt struct, `cfg.fg_type` +0x7ac vs +0x504, so even trivial functions differ by 2-5 words; monitor_workfunc 2014 words, probe 1220,
fg_update_status 958, fg_ui_soc_smooth 794, battery_shutdown_parse_dt 414 ...; total masked word diff 7824; compare_ko function set: 21 only-in-stock).
To make this module OS4-equivalent the struct strategy_fg must be re-derived from the OS4 BTF (`START_ID=141904 python -I work/re/a17A_scripts/btf_dump.py
local <work-dir>`) and the 21 new functions reverse engineered. Not started (budget).
The three "last functions" are the same code in OS3 and OS4 for get_first_usage_date; get_manufacturing_date has 2 more log lines in OS4.

New finding for `mca_strategy_get_first_usage_date` (works for any `memcmp`-vs-literal case): the date buffers are 9 bytes each (`date[2][FG_DATE_LEN + 1]`, 8 digits + NUL;
OS4 zeroes 18 bytes, the slave date is at +9, the log line prints them with %s). With 8-byte buffers the alloca gets 8-byte aligned by the
InstCombine alignment enforcement of the `memcpy(dst, date[0], 8)` -> `load i64` conversion, which makes InstCombine fold the master `memcmp(.., "99999999", 8)`
early (LLVM 17 needs known 8-byte alignment of the operand), the slave one at +9 stays a call until ExpandMemCmp, and GVN forwards the master bytes from the i64 load:
the stock has BOTH memcmps expanded late (byte loads from the stack, `cset` booleans). Selecting the source with a variable index (`memcpy(dst, date[idx], 8)`)
prevents the alignment enforcement and reproduces the late expansion: 95 vs 97 insns now (46 masked words, of which 3 are the OS4 struct offsets). Remaining: the stock keeps
the final `x8 = {0,9}` phi as branches (no `cset ls`/`orr x8,x8,x8,lsl#3` select), load order [sp] before [sp+9]. Variant tester: `work/re/g2b_tools/fgq.sh` + `<work-dir>`.
Charging/fuel-gauge module: community-only source, never flash, never put into a ROM build.


## G2c (17.10.2026): not worked on

Not attempted in round G2c (token budget went to `mca_charger_thermal` and `sc8581_sy6541`). State unchanged. Tools that exist for the next agent: `work/re/g2c_tools/` (`fgq.sh` multi-TU ThinLTO emulation loop against the OS4 stock with scratch `<work-dir>`, `pb_pin2.py` auto-pinner that also works for duplicated symbol names, `prune.py` deletes unused statics, `splice.py`, `fvar.py` source-variant tester, `pad_g2c.py` GENKSYMS-only CRC pad forger) and the "separate tiny static helper per register update" layout trick documented in `src/sc8581_sy6541/STATUS.md` (explains `tbz` vs `tbnz` shape differences of inlined `cp_update_bits`/`update_bits`).
