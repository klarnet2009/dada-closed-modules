# miloadtrace - PARTIAL (74 of 83 functions) vs OS3 only; no OS4 counterpart (module vanished, g5 check 17.10.2026)

HyperOS 4.0 has no `miloadtrace.ko`. Strings `frame-load by David`, `migov` occur in no OS4 module; the frame-load tracking ('flt') lives on in the new OS4 module `mi_game` (`kernel/sched/xiaomi_sched/qcom/mi_game/flt.c`, 'by David' author strings in `mi_game` and `mist`) and parts of the governor hooks in `sched-walt`/`perf_actuator`-style modules (17 of 82 function names overlap with `sched-walt`, mostly generic names). Nothing to compare against OS4 here; the successors belong to the G1 group. The 9 functions that still differ from the HyperOS 3 binary are described below (unchanged).

---

## Earlier notes (HyperOS 3 round, kept)

# miloadtrace (perfC) -- status: PARTIAL, 74/83 functions byte-identical

Module: "frame-load by David" (group/frame load tracker + `migov` cpufreq governor, hooks into mi_schedule / sched-walt / metis).

Build: Kleaf ddk_module `//re/miloadtrace`, `miloadtrace_defconfig` = ThinLTO (`CONFIG_LTO_CLANG_THIN=y`), `-ffunction-sections -fdata-sections`, three TUs:
`frame_weight.c` (frame tracker, ioctl, proc, init/exit), `cpugov_mi.c` (governor) and `miload_trace.c` (tracepoint definitions in their own TU: with
`CREATE_TRACE_POINTS` inside frame_weight.c clang inlines `__traceiter_*` into the `trace_*` wrappers and stops inlining the wrappers into callers; stock keeps `bl __traceiter_*`).
`local_defines PERFC_FORGE_CRC=1`. Imports come from stub modules (`re/stubs_perfC`: `mi_schedule_stub`, `sched_walt_stub`, `metis_stub`).

compare_ko (real Bazel build, `work/scripts/perfC/pb.sh miloadtrace`), last run:
- function set: OK (83); function relocations (resolved targets): OK, all identical; global objects (name,size): OK
- exports + CRC: OK (1 export, forged, see below); imports + CRC (`__versions`): OK (122; 25 forged, see below); modinfo: OK
- function bytes: DIFF in 9 functions, listed below; 74 are byte-identical

## Functions that still differ (ck.sh miloadtrace thin, diff lines vs stock)
| function | diff | state |
|---|---|---|
| mi_frame_load_ravg | 1282 (stock 1623 insns, built 1601) | fully reconstructed (task ravg roll, group ravg, cpu ravg, global frame roll); every helper call count equals stock; ~430 diff lines when register names are ignored: cold-block order (stock puts the down-boost callback, irq_work_queue_on, flw_debug prints at the end by frequency), register allocation, csel vs branch |
| get_group_util | 971 (802 / 809) | fully reconstructed; block order [cluster, LS, per-cpu] now as stock (unlikely/likely hints), frame 0x150 vs 0x160, policy pointer spilled in stock, early-return call block shared in stock, `cmp #1; b.hi` vs `cmp #2; b.hs` on cid, margin-test order |
| mi_frame_ioctl | 916 (1266 / 1246) | fully reconstructed (cmd 2 frame start, cmd 3 set fps); stock hoists the `freq_target_delta_flag` page address into x25 (MachineCSE), bounds-check forms, cold block order |
| mi_gov_update_freq | 477 (428 / 427) | fully reconstructed in cpugov_mi.c; register-normalised diff still 213 lines |
| mi_update_group_group | 232-260 | semantics complete; stock tests event 0,3,2,4 as a plain chain (no switch, no jump table); SimplifyCFG turns every source form into a switch (emitted 3,2,0) or jump table; best form is an else-if chain |
| set_update_fps | 131 | loop layout: stock keeps the list-walk latch after the body and the found block after the else path; ours rotates the latch to the top (same IR for ~25 source forms) |
| group_frame_cpufreq_transition | 16 | same latch placement issue (MBP: the cold flw_dbg printk block ends up at the bottom of the loop chain, which triggers rotateLoop; stock does not rotate) |
| init_module | 4 | one compare: stock `cmp x13,#0x20; b.hs`, ours `cmp #0x1f; b.hi` (see lesson 1: stock has the loop exit block BEFORE the body in IR order, ours after; I found no source form that changes that) |
| migrate_inter_cluster_fload | 2 | one `ldr x8,[x28,w19,uxtw #3]` vs `[x28,x26,lsl #3]` (zext sharing of the cpu index). The 4-line bound-check polarity diff was fixed by passing `per_cpu(walt_rq, cpu).cluster` into update_cluster_load_subtractions() |

## What was reproduced exactly (74 functions)
Governor (29 of 30), 12 trace functions, proc fops, load subtraction helpers, `find_cal_divisor`, `get_cluster_cpu_max_util` (= `find_cal_divisor(pred, cpu_fps_period, target)` inlined),
`add_to_task_load`, `add_to_grp_frame_load`, `update_grp_period`, `init_grp_freq_record`, `rollover_task_grp_window`, `migov_run_callback`, `fix_fload_in_irq`, `flw_irq_work`,
`frame_load_exit`, `skip_walt_window_update`, `update_gloabal_target_period`, ... (the "MISSING" helper lines of ck.sh are inlined in stock too).

## Forged CRCs (documented hacks, not genuine)
- Export `group_frame_cpufreq_transition` (0xc58f3d35): the stock include set is not recoverable. In frame_weight.c a genksyms-only stand-in declaration
  (`#if defined(PERFC_FORGE_CRC) && defined(__GENKSYMS__)`) carries a salt string literal chosen so that crc32(tokens) equals the stock CRC (`work/scripts/perfC/crcforge.py`); the real body is compiled otherwise.
- 25 of the 45 imports of the stub providers (mi_schedule / sched-walt / metis; vendor struct layouts only partly known) use the same forging via `work/scripts/perfC/forge_stubs.py`
  (functions: stand-in hoisted above the includes because genksyms keeps the FIRST declaration it sees; variables `walt_rq`, `mi_runqueues`, `part_haltable_cpus`: in place with `#ifdef __GENKSYMS__ extern char name[sizeof("salt")]; #else real #endif`).
  The other 20 import CRCs are genuine.
- Other source hacks (no code effect, only codegen): the anchor `flw_work_in_process = 1 + skip_walt_result[flw_work_in_process & 1] * 0;` in flw_irq_work (lesson 1);
  `likely()/unlikely()` hints found by measured sweeps inside the three big blocks; `update_grp_period` is `noinline` with 3 params; `struct rq` got `cpu_capacity_orig`.

## Lessons (reusable)
1. **Compare polarity (`cmp #N; b.hs` vs `cmp #N-1; b.hi`) comes from IR block order.** SelectionDAG inverts a conditional branch when its true successor is the next block of the IR function,
   and the inverted `uge N` is canonicalised to `ugt N-1`. clang emits the shared UBSAN array-bounds trap block right after the FIRST check in source order, so a loop whose own
   `__per_cpu_offset[cpu]` check is the first one in the function gets the trap block between header and body: no inversion, `hs #0x20` (when the trap block stays alive thanks to a later check).
   An earlier `arr[var]` (even optimised away) flips it to `hi #0x1f` (flw_irq_work). A callee inlined from a helper keeps ITS trap block position (migrate: pass the cluster in so the helper's first check is `curr_window_cpus[i]`).
   Moving the exit block before the body in the IR text (`clang -x ir -Xclang -disable-llvm-passes`) reproduces stock's `hs` for init_module. Tools: `ckll.sh` (optimised IR), `-mllvm -print-after=finalize-isel`.
2. Chains of `==` tests on one variable are turned into a switch by SimplifyCFG and emitted in DESCENDING value order. To get stock's order 1,3,2 write `x = default; if (f == 2) .. else if (f == 3) .. else if (f == 1) ..`
   (find_cal_divisor, find_freq_period). A two-case chain is emitted in the reverse of the source order (add_to_grp_frame_load: `== 9` before `== 10`; add_to_task_load: `event == 0` before `event == 2`).
   likely()/unlikely() or `__builtin_expect(x,0)` on conditions of a chain peel the hot case first and decide whether a jump table is used (see `gen_hints.py`, `hints_seq.py`).
3. Small helpers that look redundant are real: get_cluster_cpu_max_util is literally find_cal_divisor() with other arguments.
4. Cache a field in a local at the top when stock keeps it in a callee-saved register across a call (add_to_task_load: `u32 rsv = mi_tsk->ravg.rsv;`).
5. Tracepoints: define them in a separate TU. `-fno-jump-tables` is NOT what separates stock from our builds.
6. MBP: cold blocks go to the end of the loop chain; if the exiting latch is then not the chain bottom `rotateLoop` moves the latch to the top. Cold blocks are laid out in descending block-frequency order;
   `-mllvm -print-before=block-placement` (via `ld.lld -r`) shows the branch probabilities, `-print-machine-bfi` the frequencies. Inline-vs-call of `refcount_inc`/trace wrappers follows the call-site frequency (< ~2% of entry = not inlined; `[[clang::always_inline]]` on the call forces it).
7. Placeholders that only write globals are needed while reconstructing: write-only statics are deleted by clang.

## Tools
`work/scripts/perfC/`: `ck.sh`, `pb.sh`, `vsearch.py` (+ `gen_spec_*.py`, `gen_hints.py`), `nd.sh`/`perfC_nd.py` (register-normalised diff), `ckxa.sh` (disassemble test functions of a scratch dir),
`ckll.sh`, `splice_blocks.py`/`retake_block.py` (move helper blocks between dirs; the three big functions sit between `/* ---- <name> block begin ---- */` markers), `crcforge.py`, `forge_stubs.py`.
Scratch dirs `work/re/src/perfC_*` (and server `<build-dir>`, `<build-dir>`) can be deleted.
