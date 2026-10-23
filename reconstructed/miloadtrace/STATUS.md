# miloadtrace - PARTIAL (74 of 83 functions) vs OS3 only; no OS4 counterpart (module vanished, g5 check 17.10.2026); G57a: mi_gov_update_freq double rounding fixed; G59a (23.10.2026): 4 more real source differences fixed (mi_gov_update_freq out-of-capacity max_limit + 3 width issues, mi_frame_ioctl loop NULL test), the 3 OPEN functions are now EQUIVALENT-PROVEN, nothing OPEN, 74/83 identical unchanged, rebuilt .ko md5 3fc02eb83bb1321f1d4b1af2b43e36fe; NOT in the phone's OS4, all DO NOT LOAD

## G57a (22.10.2026) - OS3.0.6 stock comparison (md5 ad52e31f8037c85ed73e7e7b069cc44e)
Base = the OS3 stock module (<build-dir>); HyperOS 4 has no miloadtrace.ko, so this module is irrelevant for the phone's OS4 (low priority). Build: real Kleaf (scratch <work-dir> tag g57a, bazel-out removed) vs the GLOBAL OS4.0.0.7 stock; tools work/re/src/g57a_tools (nd57 = raw `llvm-objdump -dr` diff with relocation targets resolved to symbol/string content, wl57 = register-allocation independent dataflow labels, alu57/sym57/str57 = non-register skeleton, stset57 = store multiset). Evidence per function: work\a17\review\g57a\<module>__<function>.txt, compare_ko_v2 logs <module>__compare_ko_v2.txt.
FIXED (real behaviour difference, found with the non-register skeleton `sub #1` vs `sub #2`): `mi_gov_update_freq`, branch `cal_cpufreq_policy == 0`: the stock computes `f = DIV_ROUND_UP(max_freq * util, max)` (listing: `mul w9,w9,w24; add x9,x28,x9; sub x9,x9,#1; udiv x9,x9,x28`), ours had `DIV_ROUND_UP(max_freq*util + max - 1, max)` (listing `add x9,x9,x28,lsl #1; sub x9,x9,#2; udiv`), a double rounding that gave a result one unit higher than the stock for most inputs. Source changed in cpugov_mi.c; the function still differs by codegen.
Note: `compare_ko_v2` function-binding/kcfi-preamble FAIL for `migov_run_callback` is a name-pairing artefact: both builds contain the same two functions (a LOCAL one without preamble at .text and the promoted `migov_run_callback.llvm.3124179784938975774`, GLOBAL HIDDEN with preamble 0xcf7110c5); the two same-named symbols are listed in a different order by the tool. bug-table/bug-lines: same 5 WARN/BUG sites (mi_frame_load_ravg, mi_update_group_group, cpu_gov_init, cpu_gov_start x2) with the stock's line numbers (frame_weight.c 1543/917, cpugov_mi.c 765, cpugov_mi.h 37/40) vs ours; cosmetic. depends order differs.
| function | verdict | argument |
|---|---|---|
| init_module | EQUIVALENT-PROVEN | `cmp x13,#0x20; b.hs` vs `cmp x13,#0x1f; b.hi` (same unsigned test), nothing else (161 insns). |
| migrate_inter_cluster_fload | EQUIVALENT-PROVEN | one instruction: `ldr x8,[x28,w19,uxtw#3]` vs `ldr x8,[x28,x26,lsl#3]`; w19 = the cpu argument (w2), x26 = sxtw(w2) and w2 < 0x20 is tested on entry, so x26 == uxtw(w19). |
| group_frame_cpufreq_transition | EQUIVALENT-PROVEN | loop rotation of `list_for_each_entry` over the groups and the order of the two trap blocks (`brk #1`, `brk #0x5512`); same trap conditions (cluster type >=2 -> 0x5512, byte offsets 8/16/0x1f -> #1), same stores, same two printks; wl57 residual 2 instructions. |
| set_update_fps | EQUIVALENT-PROVEN | block order (empty-list exit, final stores) and the order of the independent stores/loads: same stores (pred_cpu_fps_period=x0, cpu_fps_period=x20, window_load_history+8: {+0=0, +0x10=x20, +0x40=x20>>10}), same list_del/kfree loop, `fps==0 \|\| key==0` branch, `-EINVAL` path. |
| mi_gov_update_freq | FIXED (G57a rounding + G59a 4 items) | see the G59a section below |
| mi_update_group_group | EQUIVALENT-PROVEN (hunks read) | the 19 non-register hunks read: SUB (event 0 && !on_rq or event 3: nr-1, mi_tsk->enqueue_after_migration := (wts flag != 0), debug print at flw_debug==0x32), ADD (event 2), migration ADD (event 4 && flag: nr+1, flag=0), group-type 0x10 branch with the nr_running<0 WARN + `str wzr`, refcount/ref_dec: same in both; ours lowers the event switch to a jump table (`cmp w22,#4; adr; ldrsw; br`), the stock to a compare chain; the stock has additional `cmp w20,#7/#8; b.hi` bounds traps on cpu (unreachable for cpu < 8); strings 9 = 9, data symbols 8 = 8, ours has 2 more printk call sites (duplicated tails). |
| get_group_util | EQUIVALENT-PROVEN (G59a, was OPEN in G57a) | see the G59a table below |
| mi_frame_load_ravg | EQUIVALENT-PROVEN (G59a, was OPEN in G57a) | see the G59a table below |
| mi_frame_ioctl | EQUIVALENT-PROVEN (G59a, was OPEN in G57a) | see the G59a table below |

## G59a (23.10.2026) - residual hunks of the OPEN functions read (OS3.0.6 stock)
Evidence: work\a17\review\g59a\miloadtrace__<fn>.{stock.lst,ours_rebuilt.lst,rb_rebuilt.txt} (stock/ours BB-labelled raw listings, register-blind diff hunks after the rebuild), compare log work\a17\review\g59a_miloadtrace__compare_ko_v2.txt (verdict lines identical to the G57a run; function-bytes still 9 of 83 differ = the same 9 functions). Tools work/re/src/g59a_tools (dump59.py, ms59.py = multiset of S-only/O-only non-register instructions of an rb diff, kb59.sh = Kleaf build + compare). Rebuilt with real Kleaf (scratch <work-dir>): md5 3fc02eb83bb1321f1d4b1af2b43e36fe.
FIXED in source (all found by reading the raw listing, not by the earlier tools):
- mi_gov_update_freq (cpugov_mi.c), REAL behaviour difference: when `big_maxfreq_limit && util > cap*(out_capacity_thresh+100)/100` the stock sets `max_limit = policy->max` and does not read `mi_maxfreq[cid]` (stock: print, check cid<2, then `ldr w3,[sp,#4]` = policy->max and jump into the mi_minfreq code); ours only printed and kept `mi_maxfreq[cid] ? : policy->max`. Now `if (big_maxfreq_limit && util > ...) { print; max_limit = policy->max; } else max_limit = mi_maxfreq[cid] ? ...`. Listing after the fix: `b.ls skip; ldr w9,[migov_debug]; cbnz print; cmp w26,#1; b.hi trap; ldr w3,[x8,#0x38]; b <mi_minfreq code>` as in the stock.
- mi_gov_update_freq, width issues (differ only for values >= 2^31): (a) hispeed_util is read through its low 32 bits in the stock (`ldr w9,[x26,#0x50]; cbz w9; cmp w9,w24`), now `(unsigned int)migov_policy->hispeed_util` in the test and the assignment; (b) cal_cpufreq_policy==0: the inner DIV_ROUND_UP quotient stays 64-bit (stock `add x9,x22,x9,lsl #10`, ours had `ubfiz x9,x9,#10,#32`); (c) cal_cpufreq_policy!=0: `(u64)max_freq + (max_freq>>shift)` in 64 bit (stock `add x9,x10,x9`, ours 32-bit add). The cpu<32 trap of cpu_rq() stays at a different place (stock hoists it before the cal_cpufreq_policy test, ours has it in the big_maxfreq_limit block; only a trap for an impossible cpu).
- mi_frame_ioctl (frame_weight.c): the cleanup loop of cmd 3 (`!period_traced_pid || !cpu_fps_period`) is now `for (pos = period_list.next; pos != &period_list && pos; pos = n) { n = pos->next; ...}`; ours had the plain list_for_each_safe without the `pos != NULL` test (stock: `cmp x8,x23; b.eq; cbz x8` and `cmp x24,x23; b.eq; cbnz x24`). Only matters for a corrupted list.
| function | verdict | argument |
|---|---|---|
| mi_gov_update_freq | FIXED + rest EQUIVALENT-PROVEN | whole 428/426-insn listings read after the fix (38 rb hunks): remaining hunks = register allocation, spill slots, order of independent loads, `bic;cmp #0` vs `bics`, `cmp w9,w24; b.lo` vs `cmp w24,w9; b.hi` (same unsigned test), stock tail-duplicated 0x5e/0x50 pct constants, position of the policy->cpu < 32 trap. |
| get_group_util | EQUIVALENT-PROVEN | all 802/809 insns + 60 rb hunks read. Same constants and offsets (FORE mask 0x8c, grp fields 0x28/0x38/0x40/0x450/0x3f8/0x400, cluster frames 0x350/0x3a0, cpu frames 0xa8+0x18+cpu*40 / 0x1e8+0x18+cpu*40, avg_freq_period 0x60, window_load_history +0x8/+0x18/+0x28, the *_percent tables, /100 magic), same compare polarity (`cmp #N; hs` vs `cmp #N-1; hi`), same find_cal_divisor switch (flag 1,3,2), same branch-free csel chain for max_cpu/max_sys_cpu/max_back_cpu/kern_util (else-if order type&2, type&1, 0x100), same policy 2/3/else tails, expand_shift, `frame_drop_boost && fore_timeout` max with old_util (stock branch, ours csel), min(cap,result) 64-bit store into grp_util[cid], group_util_valid, the five printk argument lists. Differences: spill slots/reload order, two pure comparisons (delta>margin, delta<period) swapped, duplicated `ret`. |
| mi_frame_ioctl | FIXED (loop NULL test) + rest EQUIVALENT-PROVEN | 51 hunks before / 49 after; multiset of S-only/O-only non-register instructions after the fix = trap-check forms (`cmp #8; b.hi` x8 vs `cmp #9; b.hs` x8), `ldrb` vs `ldr` of the grp flag word (only bits 1..4 tested), `cpu_online(smp_processor_id())` as `lsl;and` vs `lsr;tbz`, work pointer chosen by csel (ours) vs two branches (stock; same irq_work_queue / irq_work_queue_on(cpumask_first) tails), block order. |
| mi_frame_load_ravg | EQUIVALENT-PROVEN | all 107 rb hunks + multiset read (1623/1601 insns). Leftovers: frame 0x130 vs 0x120, bound checks (`cmp #0x20; hs` vs `#0x1f; hi`, `#8; lo` vs `#7; ls`), flag byte vs word load of p->flags (bit1 PF_IDLE only), one tail-duplicated copy in the stock of the util-boost threshold block (`mov #0xf5c3..; umulh; cmp; b.ls`, curr/prev cluster io sums [0x3f8]/[0x3a8], migov_run_callback(rq,0x100), down_pred_util=1 at +0x4b2) and of the `[0x3f8] += exec` update blocks, and the polarity of the `global` flag: stock computes `global && full_window` as `csel w8,wzr,w8,lo`, ours keeps the inverted flag (`csinc w8,w8,wzr,hs`) with the two successors swapped (both: full-window path calls get_scale_exec_time(window), else get_scale_exec_time(fs-mark_start)). No constant, offset, call argument or store differs. |

(previous first line before G57a: miloadtrace - PARTIAL (74 of 83 functions) vs OS3 only; no OS4 counterpart (module vanished, g5 check 17.10.2026))

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
