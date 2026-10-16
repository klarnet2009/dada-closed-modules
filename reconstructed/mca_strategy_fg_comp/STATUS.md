# mca_strategy_fg_comp - STATUS: PARTIAL (73 of 76 functions byte-identical)

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
