# Polarity audit of the charging family (goal-G21a)

Base: GLOBAL OS4.0.0.7 stock (`<work-dir>`). Community-only, DO NOT LOAD, no safety claim, nothing flashed,
no module source edited, no Bazel. Tool: `work/a17/review/tools5/symcmp5.py` (new; symcmp4 untouched). Scratch (server): `<work-dir>`
(`rep/all_<module>.txt` = full per-function output, `batch.sh`, `show.py`).

## 1. Where symcmp4 lost the polarity (read of symcmp4.py)

* `Fn.step` emits compare facts as `cond(key)` with **no truth value**; `lit()` folds `ge/le/ls/pl/vc/nz` into the class `lt/gt/hi/lo/mi/vs/z`
  and keeps the truth only inside the guard literals.
* The verdict logic (`SUMMARY ... => REVIEW / COND-FORM / CLASS-A-auto`) escalates to REVIEW only for effects, guards and `ret(...)`;
  `cond-form-diff` (compare facts present on one side only) gives just **COND-FORM**, never REVIEW.
* `merge_vals` merges values into an unordered `phi{a|b}`: `ret(phi{0|1})` is equal whatever the conditions are (this is the
  `mca_cp_mode_in_range` hole: a missing second range variant only removes cond facts and changes which constant is returned on which path).
* A `ccmp` chain is an opaque key `ccmp(prevkey=truth, innerkey, nzcv)` (inner operand order is already sorted away).
* Guards are only **must-guards** (intersection over all occurrences of a fact), so a path condition that differs on one of several occurrences is invisible.
* `tailcall(fn)` carries no arguments.

## 2. symcmp5 (what it does)

Every compare (cmp/cmn/tst/cbz/cbnz/tbz/tbnz/b.cc/csel/csinc/cset/cinc/ccmp/ccmn, constants folded, materialised bools and `orr/and/eor/add/sub`
of bools folded) becomes an exact boolean formula over canonical atoms (operands order-normalised, `x<=4`->`x<5`, signedness kept: `lt` and `lo`
are different atoms, branch sense carried by the literal sign). Per block the exact reach condition is a BDD (loop back edges ignored). Values
merged at joins are gated: `G{leaf@reach|...}`. A fact (call/st/stkarg/atomic/msr/tailcall(with x0..x7)/trap/insn/ret leaf) is compared by the OR of
its reach conditions between the builds, modulo mutual exclusion (`lt/gt/eq`, `lo/hi/eq` of one operand pair; `eq c1` vs `eq c2`, `eq c`/`lt n`/`lo n`
implications). Verdicts per function: `POLARITY-DIFF` (same compares on both sides, different logic; both regions are printed as cubes of aliased atoms with the
nearest flag-setting asm of each build), `POLARITY-DIFF-ATOMS` (same, but some compare atoms exist on one side only: either a genuinely missing/other
compare or operand-form noise, needs a read), `FACT-DIFF` (fact on one side only), `CLEAN-POL`. `symcmp5.py <stock> <ours> all` runs every common function
whose instruction text differs (SIGALRM 150-240 s per function).

Known limits: loops are approximated (header values collapse to PHI, address reassociation `add.x(a0+k,i)` vs `add.x(a0,i)+k` and `ldp`/`ldr` pair loads are not
fully unified, so loop-heavy functions give POLARITY-DIFF-ATOMS noise); atom operands are phi/out/in_x-normalised (as symcmp4 nphi); value arithmetic other than
bool algebra is compared as text; BDD cap 700k nodes -> INCONCLUSIVE; the tool says nothing about memory ordering or stack data beyond symcmp4's facts.

## 3. Validation

1. **Synthetic** (`tools5/selftest/t_a.c` stock, `t_b.c` planted bugs, `t_c.c` equivalent rewrites; clang r510928 aarch64 -O2/-Os/-O1; 9 functions):
   * stock vs `t_b` (planted `&&`->`||`, `<`->`>=`, signed->unsigned, `<`->`<=`, 20->21 boundary, bool `&&`->`||`, csel arg swap, second range variant dropped): **8 of 8 reported**
     (POLARITY-DIFF or POLARITY-DIFF-ATOMS), 1 equivalent function (`diamond`, other source form + `__builtin_expect`) CLEAN.
   * stock vs `t_c` (operand-swapped / negated / reordered source), stock -O2 vs stock -Os (other regalloc and block order), stock vs `t_c` -O1 (ccmp vs branch chains):
     **0 of 9 reported in each of the 3 pairs** (all CLEAN-POL). Before the `orr/csinc/cinc` bool folding 2 false alarms existed (`in_range`, `bool_chain`); they are gone.
2. **Real bug** `mca_cp_mode_in_range` (inlined into `mca_select_cp_work_mode_by_current`): pre-G20d build `<work-dir>` vs global stock:
   symcmp4 = `COND-FORM` (cond-form-diff 31, effects/guards/ret equal, `CLASS-A-phi`); **symcmp5 = POLARITY-DIFF (6 differing facts: ret(-1/0/1/2), ret(ldr(a0+2904)), st to a0+2904)**.
   The post-G20d build (`mca_strategy_quickchg.ko`) still shows only POLARITY-DIFF-ATOMS for this function (5 diffs, 0 on shared atoms): the remaining differences are loop
   address forms (`ldp` pair vs `ldr`, `add.x(a0+2916,i)` vs `add.x(a0,i)+k`), the same codegen difference G20d wrote down; no logic difference on shared compares.
   The gate difference of the store to `a0+2904` that was reported in an earlier tool revision was a missing mutual-exclusion fact (`mode==2` vs `mode==4`), now in the care set.

## 4. Run over the charging family (all functions whose text differs, one at a time, nice 10, ulimit -v 16G)

Builds used (newest per module): quickchg/quick_wireless/basic_wireless/business_charger_comp `g20d/built`; smart_charge and bq27z561 `g17b/built`; strategy_buckchg and
strategy_fg_comp `g14e/built`; mca_buckchg_jeita and mca_wireless_revchg `g14b/built` (**no newer build exists; built before the global port, so this is a result for an old build only**).
Not touched by G14e/G17b/G20d since: note for the two g14b modules.

| module | common funcs | text-identical | compared | POLARITY-DIFF | POLARITY-DIFF-ATOMS | FACT-DIFF | CLEAN-POL | inconclusive/err |
|---|---|---|---|---|---|---|---|---|
| mca_strategy_quickchg | 83 | 21 | 62 | 2 | 5 | 6 | 48 | 1 (regulation: timeout) |
| mca_quick_wireless | 37 | 9 | 28 | 0 | 1 | 0 | 26 | 1 (select_max_ibat) |
| mca_basic_wireless | 73 | 15 | 58 | 2 | 0 | 1 | 55 | 0 |
| mca_business_charger_comp | 45 | 12 | 33 | 1 | 0 | 2 | 30 | 0 |
| mca_strategy_buckchg | 62 | 13 | 49 | 3 | 2 | 4 | 40 | 0 |
| mca_strategy_fg_comp | 99 | 44 | 55 | 1 | 6 | 8 | 40 | 0 |
| mca_smart_charge | 38 | 16 | 22 | 2 | 0 | 1 | 19 | 0 |
| bq27z561 | 200 | 103 | 97 | 5 | 11 | 7 | 74 | 0 |
| mca_buckchg_jeita (g14b build) | 17 | 4 | 13 | 2 | 0 | 0 | 10 | 1 (monitor_work: timeout) |
| mca_wireless_revchg (g14b build) | 45 | 20 | 25 | 0 | 0 | 0 | 25 | 0 |

"text-identical" is strict (branch targets count), so it is lower than the "byte-identical" numbers in STATUS.md.

### 4.1 Findings read by hand (stock listing vs ours/source) - verdicts

| function | tool | verdict | evidence |
|---|---|---|---|
| mca_select_cp_work_mode_by_current (quickchg) | POLARITY-DIFF-ATOMS 5 | **CODEGEN** (post-G20d) | no diff on shared compares; only loop address forms / `ldp` vs `ldr`; pre-G20d build is caught as POLARITY-DIFF (see 3.2) |
| mca_check_cp_work_mode (quickchg) | was POLARITY-DIFF 1 | **CODEGEN** | only the reach of `__stack_chk_fail` (canary compare `x8 == SP_EL0+1568` is an atom); folded as canary-equal in the tool, now FACT-DIFF only |
| strategy_buckchg_process_antiburn_change | POLARITY-DIFF 4 | **CODEGEN / log-only** | inlined `set_charge_volt(info,5V)`: stock logs `pwr_cap nums is null` with **line 2041 for types 11,12 and line 2033 for type 10**; ours logs line 2041 for type 10 too (A6 `type==10` is an explicit test in ours only); votes/stop/start identical. Only the `__LINE__` argument of a log differs. |
| business_charger_report_quick_charge_type_work | POLARITY-DIFF 1 (gating of the store to `a0+416`) | **CODEGEN, medium confidence** | stock at `.text+0x50..0xdc` (`cbz w3` on a0+505 at 0x1750, direct `str w4,[x19,#0x1a0]` at 0x17d8, `csel gt` + store at 0x1760): `keep_report_max_power` (a0+505) == 0 -> direct store of `power_max`, != 0 -> `max(old,new)` (`csel gt`); source lines 607-622 have the same shape. The gate differs only in which stack-slot leaf carries the direct store (tool leaf naming); not proven formally. |

### 4.2 Findings NOT read (tool evidence only, no verdict) - follow-up list, by priority

Evidence for each: `<work-dir>` (cubes with atoms and the nearest compare asm of both builds); `python3 show.py <module> <function> 2`.

* quickchg: `mca_quick_charge_monitor_work` POLARITY-DIFF 10 (+4 atoms; 102/109 facts only-stock/only-ours, large and loop-heavy: probably mostly noise), `mca_quick_charge_adjust_adapter_mode` POLARITY-DIFF 1
  (reach of `st(str.w; ...-4; 0)` in the adp_mode_power cap chain; G20d changed these constants (21000/11000/5500), re-read the tail).
* buckchg: `strategy_buckchg_monitor_workfunc` POLARITY-DIFF 13 (+4 atoms), `strategy_buckchg_set_charge_volt` POLARITY-DIFF 2 (gating only inside the second G value of the log call: line-number-like constants, probably log only).
* fg_comp: `strategy_fg_monitor_workfunc` POLARITY-DIFF 10 (31/53 only-stock/only-ours facts, noisy), six atoms-type functions (`isc_*`, `li_plating_work`, `update_vcoutoff_v2_work`).
* smart_charge: `smart_charge_check_bypass_status` POLARITY-DIFF 3 (reach of the log `for_bypass Debounce`: stock logs when `once_flag != 0`, `a0+1632 == 0` and the debounce time has not elapsed; ours does not log in that region; log-only, LOW),
  `smart_charge_handle_sense_chg` POLARITY-DIFF 3 (gating of the leaves 0/4 of the `lsr.w(...)` value stored to `a0+692`; the sense-charge section decode, needs a read).
* bq27z561: `fg_read_time_ht` / `fg_read_time_ot` POLARITY-DIFF 11 each (reach of `__fg_mac_read_block` / `ret(0)` differs when `fg_vendor` is not in 1..4: stock proceeds, ours not or vice versa; vendor on dada is 4, so unreachable on dada, LOW),
  `fg_set_one_co` 2, `fg_get_one_cc_coulomb_integral` 1 and `fg_read_manufacturing_date` 1 (gating of the `49/48` date byte; the gate formulas are the same atoms in other order), 11 atoms-type functions (`bq_fg_probe`, `fg_sysfs_show/store`, `fg_mac_write_block`, ...: large, mostly operand-form noise).
* basic_wireless: `strategy_wireless_process_event` POLARITY-DIFF 17 (gating of the PHI/a1 argument of `_raw_spin_unlock`, ours has fewer leaves: spinlock argument, looks codegen), `strategy_wireless_process_irq` POLARITY-DIFF 6 (reach of the log `Q value is smaller than Q2`).
* jeita (old g14b build!): `mca_buckchg_jeita_probe` 1, `mca_buckchg_parse_dynamic_iterm_by_name` 3: reach of `mca_parse_dts_*` calls differs on shared atoms; **re-run on a global-port build before reading** (build predates the port).
* INCONCLUSIVE (timeout/BDD cap): `mca_quick_charge_regulation` (it was only CODEGEN-by-transitivity in G20d), `mca_wireless_quick_charge_select_max_ibat`, `mca_buckchg_jeita_monitor_work`.
* FACT-DIFF (facts on one side only, no reach difference): 29 functions (`fg_update_status`, `strategy_buckchg_class_probe/set_vterm/process_event`, `mca_quick_charge_parse_*`, `bq` `fg_get_app_checksum`, `fg_set_one_dynamic_fv`, ...): same class as the old symcmp4 only-stock/only-ours lists, not reviewed here.

## 5. Conclusions and fix proposals

* **No new BEHAVIOUR finding was proven** in this run: of the 4 hand-read items 3 are CODEGEN and 1 is a log line number. No source was edited, so no fix is proposed for a proven finding.
* Candidate list for the next reviewer (not proven): quickchg `adjust_adapter_mode` tail, smart_charge `handle_sense_chg`, bq27z561 `fg_read_time_ht/ot` for `fg_vendor` outside 1..4, buckchg `monitor_workfunc`. If a read confirms a difference, the fix is always the
  same: make the source condition match the compare form shown by the tool for the STOCK side (atoms A<n> with the nearest asm are in the report), then re-run `symcmp5.py <stock> <new> <function>` until CLEAN-POL.
* Recommended standard check from now on: run `symcmp5.py <stock> <ours> all` next to compare_ko_v2 for every module before the label CLASS-A-auto/COND-FORM is used; treat COND-FORM from symcmp4 as REVIEW.
* Not claimed: no safety claim for charging, nothing loaded or tested on hardware.
