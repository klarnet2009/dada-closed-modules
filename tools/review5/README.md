# tools5 (goal-G21a)

## symcmp5.py - polarity-faithful symbolic function comparison (extension of tools4/symcmp4.py, which is imported unchanged)

Why: symcmp4 drops the truth of compares (cond facts are class-only, phi{} merges ignore path conditions, ccmp is an opaque key), so a missing/other
range-check variant ended as `COND-FORM`/`CLASS-A-auto` (mca_cp_mode_in_range, G20d). symcmp5 keeps the exact boolean path condition of every
fact and of every merged value (BDD over canonical compare atoms) and compares them between stock and ours. Details: `../polarity_audit_charging.md` sections 1-2.

Usage (server, aarch64 .ko or .o; needs python3, tools4/symcmp4.py and compare_ko_v2.py in <work-dir>):
    cd <work-dir>            # symcmp5.py + ../tools4/symcmp4.py (or SYMCMP4_DIR=...)
    (ulimit -v 16000000; nice -n 10 python3 -I symcmp5.py STOCK.ko OURS.ko func1 func2 ...)   # named functions
    (ulimit -v 16000000; S5TMO=150 nice -n 10 python3 -I symcmp5.py STOCK.ko OURS.ko all)     # every common function whose text differs
    add -v for the count-diff list; S5DBG=1 prints atoms missing from the first pass.
Output per function: `SUMMARY5 <fn>: polarity-diff N (same-atoms a, one-sided-atoms b) | only-stock x only-ours y | count-diff z => VERDICT`
  POLARITY-DIFF        same compares on both sides, different path condition / value gating (regions printed as cubes of aliased atoms A<n>, with the nearest compare asm of each build)
  POLARITY-DIFF-ATOMS  as above but some atoms exist on one side only (missing compare, or operand-form noise: needs a read)
  FACT-DIFF            a fact (call/st/stkarg/ret leaf/...) exists on one side only
  CLEAN-POL            none of the above (does not prove equality: effects/arg values other than bool algebra are compared as text; loops are approximated)
  INCONCLUSIVE         BDD cap (700k nodes) or timeout
Treat symcmp4 COND-FORM as REVIEW; use symcmp5 `all` next to compare_ko_v2 on every module.

Self-test: `selftest/t_a.c` (stock), `t_b.c` (8 planted condition bugs), `t_c.c` (equivalent rewrites). Compile with
`clang --target=aarch64-linux-gnu -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -c -O2` (Kleaf clang-r510928, also -Os/-O1)
and run `symcmp5.py a_O2.o b_O2.o in_range inv_branch sign_cmp ret_bool ccmp_and diamond range_call bool_chain csel_pick`: expected 8 reports
(diamond clean); a_O2 vs c_O2 / a_Os / c_O1: all clean. Server copy: <work-dir>

## G25a additions (all-modules audit, see ../polarity_audit_all.md)

* `symcmp5b.py` - wrapper around the unchanged symcmp5.py: ELF check with a clear error (a 130-byte placeholder as `ours` used to crash inside compare_ko_v2), `@listfile` function lists
  (names, or `=== name:` lines of a report), `S5TMO` default 150, `--summary` verdict counts. Usage: `python3 -I symcmp5b.py STOCK.ko OURS.ko all --summary`.
* `s5digest.py` - `S5REP=<dir with all_<module>.txt> python3 s5digest.py <module>...`: one line per non-CLEAN function, with the effects that exist on one side only reduced to kinds
  (call target / store object / atomic / trap). `S-only: - O-only: -` is a strong CODEGEN hint; a call or store kind on one side is the list to read first.
* `s5ms.py` - `python3 -I s5ms.py STOCK.ko OURS.ko [f1,f2,...]`: relocation-target multiset (calls, globals) and key-instruction multiset (ldar/ldapr, atomics, dmb, brk, mrs, prfm) per function; prints
  only the functions that differ. A lost/added CALL is the evidence that matters (cld3: 133 functions, only `cm_process_join_req` and `lim_fill_pe_session` lose calls); code ORDER is meaningless.
* Known limits found, NOT fixed: (1) `ret(x)` vs `ret(0)` on a path where `cbz/cmp #0` already proves x==0 is reported as POLARITY-DIFF [reach] ret(...) (mi_game game_lb_control,
  game_choose_cpu_overutiled, cameralog_ioctl); (2) loop-exit written as `i+1==n` vs `i==n-1` gives same-atoms POLARITY-DIFF (xr_qi qi_acquire_gpu_blockeds); (3) `brk #0x800` (WARN) is taken as terminal, so a
  shared stock tail vs our duplicated continuation shows as a reach difference of the tail calls (mitee tui_shm_*); (4) big functions with different inlining/loop shape give ATOMS noise
  (hundreds of one-sided atoms); (5) stock uses `ldapr` where we emit `ldar` (RCpc build flag): shows as atomic facts only on one side; (6) under `ulimit -v 16G` one cld3 function died with MemoryError
  (`wlan_crypto_build_rsnie_with_pmksa`), 10 cld3 functions hit the BDD cap / timeout; `lb`/`mi_game` modules finish in seconds, so only cld3 needs the time budget.
* Order of calls in `objdump` is address order (cold blocks such as `__put_task_struct` move out of line): never read it as execution order.
