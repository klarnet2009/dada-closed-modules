# mi_schedule - PARTIAL (90 of 100 functions) vs OS3 only; no OS4 counterpart (module vanished, g5 check 17.10.2026); G57a: kCFI 92/92 (mi_group_ls_group signature fixed); G59a: the last 2 OPEN functions (runtime_show_all, package_runtime_show) now EQUIVALENT-PROVEN from the raw listings, so every non-identical function has a verdict, nothing OPEN, no source change, .ko md5 unchanged (c5cd058619b0614f5e254a284ef39c13); NOT in the phone's OS4, all DO NOT LOAD

## G57a (22.10.2026) - OS3.0.6 stock comparison (md5 c5cd058619b0614f5e254a284ef39c13)
Base = the OS3 stock module (<build-dir> copy <work-dir>); HyperOS 4 (CN 0.6 and GLOBAL 0.7) contains no mi_schedule.ko, so this module is irrelevant for the phone's OS4 and was done at low priority. Build: real Kleaf (scratch <work-dir> tag g57a, bazel-out removed) vs the GLOBAL OS4.0.0.7 stock; tools work/re/src/g57a_tools (nd57 = raw `llvm-objdump -dr` diff with relocation targets resolved to symbol/string content, wl57 = register-allocation independent dataflow labels, alu57/sym57/str57 = non-register skeleton, stset57 = store multiset). Evidence per function: work\a17\review\g57a\<module>__<function>.txt, compare_ko_v2 logs <module>__compare_ko_v2.txt.
FIXED: `mi_group_ls_group` kCFI type hash: stock 0x8528a64a, ours 0x1c17b8a1. The type id is xxHash64 of the itanium type string, so it was brute-forced over plausible signatures: 0x8528a64a = `_ZTSFbP11task_structPiE` = `bool (struct task_struct *, int *)` (ours was `u64`); source changed, kcfi-preamble now PASS 92/92 (was 91/92). The function body (get_mi_task_struct(p); return false) was already identical.
| function | verdict | argument |
|---|---|---|
| alloc_add_mi_group | EQUIVALENT-PROVEN | 16 of 17 block signatures identical; the remaining block is the long run of zero stores into the freshly kmalloc'ed mi_group: stset57 = 134 stores (base ret<k>, offset, size, value) identical as a multiset (stock-only 0, ours-only 0); only their order and the `str xzr`/`stp xzr,xzr` pairing differs; no call or barrier in between. |
| __get_mi_task_struct | EQUIVALENT-PROVEN | 7 of 8 block signatures identical + the store block: 73 stores identical as a multiset (store order differs, fresh object). |
| reset_traced_window_show | EQUIVALENT-PROVEN | all 28 block signatures identical (wl57 residual 0); only the order/register of the five prologue loads. |
| package_compact_show | EQUIVALENT-PROVEN | the only difference is the symbol the table pointer is taken from: the stock has an anonymous `.rodata+0x1630` pointer table, ours a named local `app_com_type_str`; table content compared via relocations: {"unknow","32","64"} in both. |
| find_mi_group_by_task | EQUIVALENT-PROVEN | residual = `mov x20,xzr` hoisted in the stock, `bl _printk` tail duplicated in the stock (shared tail in ours), `ldr x20,[x19,#0x400]` after the debug printk placed differently; same call order, strings and data symbols. |
| update_pkg_load | EQUIVALENT-PROVEN | `cmp w25,#6; b.hs` vs `cmp w25,#5; b.hi` (same); the four load/store pairs [x9+0x128/0x158/0x188/0x1b8] -> [x9+idx*8+0x108/0x138/0x168/0x198] are the same set in both (address form `add x10,x9,#0x108; str [x10,x11]` vs `add x10,x9,w25,uxtw#3; str [x10,#0x108]`). |
| change_lsgroup | EQUIVALENT-PROVEN | residual confined to the head (blocks 0-12): the stock tests `ls_grp` first, ours first splits on grp==NULL; both paths read: grp==NULL & flags&7==0 -> return; ls_grp==NULL -> (grp? set flag=1); ls_grp->flags bit5 -> dec+clear; ls_grp==grp -> return; else dec+clear, grp==NULL -> BB18 else flag=0; the rest (ref inc, find_mi_group_by_task, lock, dec/inc nr, free_mi_group ...) has 79 identical block signatures at K=2; prologue saves x25 in the stock only. |
| mi_group_ioctl | EQUIVALENT-PROVEN | residual (K=0 non-register) = `mov` of the arguments, loads of data.id/data.type from the on-stack struct at different times, and the switch jump table: the `.rodata` table `{0x10,0x5e4,0x5e4,...}` vs `{0x10,0x5dc,...}` is a table of RELATIVE CODE OFFSETS of the case blocks (it is addressed by `adr x10; ldrsw; add; br x10`), not data; same 55 calls, 33 data accesses. |
| mi_user_roll_wk | EQUIVALENT-PROVEN | residual = constant materialisation (0xacbdacbd hoisted vs rematerialised, 64-bit magic 0xdcbaabcd12344321 in a register), &curr_runtime_items kept in x24 vs re-adrp'ed, address mode `str [x10,w8,uxtw#3]` vs `[x10,x8,lsl#3]` (x8 zero-extended by `ldr w8`), `subs w12,w8,#3; csinc ... hs` vs `cmp w8,#2; sub; csinc ... hi` (same), register shuffles around free_uid/_find_next_bit; same 18 calls, 13 data accesses. |
| runtime_show_all | EQUIVALENT-PROVEN (G59a) | whole 381/382-insn raw listings read (work\a17\review\g59a\mi_schedule__runtime_show_all.{stock,ours}.lst). Same: nbits = min(uid_max+1, 99999) (csinc, signed ge), usec/develop_mode block, usec==0 -> return 0, cluster offsets little 0xd8 / big1 0xa8 / big2 0x78 / sup 0x48 tested in this order with unsigned b.lo continue, equal-sum continue (v4 sums vs v5 sums, different add association, equal mod 2^64), top_* updates `cmp top,p; b.gt skip` (update when top <= p) into top_package_uid[1..4] and p_t into [0], cum_* with max(.,0) (bic), sums, bias = d*10000/total (total = sumv4-sumv5 in stock vs d_s+d_b1+d_l+d_b2 in ours, equal mod 2^64), the 11-argument first seq_printf and the second one whose argument order (bias_s, bias_b2, bias_b1 first, p_l last) is identical in both, 64BIT/32BIT csel on is_32B_user, the 7 closing printfs (signed /100 for cum_*, unsigned umull for top_*). Differences: register/stack allocation, spill slots, order of independent loads, hoisting of find_next_bit arguments. |
| package_runtime_show | EQUIVALENT-PROVEN (G59a) | whole 291/285-insn raw listings read (g59a\mi_schedule__package_runtime_show.{stock,ours}.lst). Same: uid from m->private, usec/develop_mode, window_ns = usec*1000, the 2 header printfs, rcu for_each_process/for_each_thread (offsets 0x2c8, 0x328, 0x6d8, 0x550, uid compare), mi_task cluster offsets 0x188/0x158/0x128 tested in this order (b.lo continue), `b2+s+b1 == 0` continue, then 0x1b8 (little), sums sum_s/b2/b1/l, cum_s/cum_big(p_b2+p_b1)/cum_l, bias args, both per-thread printfs element by element (stack slots sp+0..0x20), 32B/64B csel, closing printfs. is32 |= bit22: stock `ubfx;orr` vs ours `orr lsr#22; and #1` equal for a 0/1 flag; `cmn x9,x8` == stock `add;cbz` mod 2^64. Differences: register allocation / spills only. |
Not source-reachable: exports CRC 21 of 33 (genksyms over `struct task_struct` of the 6.6.57 headers), bug-lines (mi_group.c lines 810/769 vs ours; entries identical in function/file), `.altinstructions` offsets (layout).

(previous first line before G57a: mi_schedule - PARTIAL (90 of 100 functions) vs OS3 only; no OS4 counterpart (module vanished, g5 check 17.10.2026))

HyperOS 4.0 has no `mi_schedule.ko`. Strings `/dev/mi_group`, `pkg runtime info calc`, `mi_group` occur in no OS4 module. Only the task-accounting part survived, rewritten inside the new OS4 module `mi_game` (source paths `kernel/sched/xiaomi_sched/qcom/mi_game/{flt.c,mi_game_ca.c,mi_game_load.c}`): 13 of the 100 OS3 function names also exist in `mi_game` (`free_mi_task_struct, mi_task_world_init, mi_task_exiting, task_add_to_userlist, task_add_work, update_pkg_load, get_cluster_type, is_32B_user, mi_cpu_coregroup_mask, task_user, _copy_from_user, init/cleanup_module`), the group/runtime-proc parts (`mi_group.c`, `mi_runtime.c`) are gone. Nothing to compare against OS4 here; the successor `mi_game` belongs to the G1 group. The 10 functions that still differ from the HyperOS 3 binary are described below (unchanged).

---

## Earlier notes (HyperOS 3 round, kept)

# mi_schedule - PARTIAL: 90 of 100 functions byte-identical

`compare_ko.py` (ThinLTO build, `mi_schedule_defconfig`, bazel truth via `work/scripts/re_push.sh mi_schedule`):

| check | result |
|---|---|
| function set (100) | OK |
| function bytes | 90 identical, 10 differ (list below) |
| relocations (resolved targets) | OK for all 100 functions (also for the 10 that differ) |
| global objects (name, size) | OK (incl. hidden promoted statics `*.llvm.2722419413465187494`, `group_misc.llvm.5288858629294896160`) |
| imports + CRCs (67) | OK |
| modinfo | OK (`MODULE_DESCRIPTION("pkg runtime info calc by David")`) |
| export CRCs | 12 of 33 match (see below) |

## Layout of the recovered source (three translation units, original path of the first one is `drivers/mihw/mi_sched/mi_group.c`)

- `mi_group.c`   - group management (`struct mi_group` 1248 B, `ls_group`, `mi_default_group`), `/dev/mi_group` misc device + ioctl, `period_freq_list_lock`, `target_makeup_ns`, module params.
- `mi_runtime.c` - `/proc/package/...` package (uid) runtime statistics (`create_runtime_proc()`, `delete_runtimeproc()`, seq_file show/write functions).
- `mi_task.c`    - per-task / per-uid accounting (`mi_task_struct` in `task->android_oem_data1[0..1]` = {magic, ptr}, `mi_user_struct` in `user_struct->android_oem_data1`), vendor hooks
  (`android_vh_free_task`, `android_vh_scheduler_tick`), `update_pkg_load()`, the work items, `mi_sched_init/exit`.
- `mi_sched.h`   - all types from the stock BTF (sizes/offsets guarded by `_Static_assert`/`MI_OFF`): `struct mi_group`, `mi_task_struct` (1368 B), `mi_user_struct` (248 B),
  `mi_task_ravg`, `gt_task`, `mi_rq` (408 B), `package_runtime_info` (4 cluster runtime_items of 6 x u64: 4 history slots, current, traced), enums.

## Still different (function bytes; instruction counts stock vs ours)

All ten have identical callee/relocation sequences; the differences are code placement / register allocation / store ordering, not semantics.

- `find_mi_group_by_task` (4 diff lines, 312 vs 310 insns): stock presets `grp = NULL` (`mov x20,xzr`) in the entry block and keeps a `bl _printk` in the inlined `find_add_mi_group_id(0,SYSTEM)` debug-log
  arm; ours puts the NULL materialisation later and shares the printk tail. Many source shapes (early `return NULL`, final `return NULL`, merged `grp = mi_tsk->mi_grp` tail, hint combinations) all canonicalise to the same code.
- `__get_mi_task_struct` (20 lines, 102 vs 102): the long run of zero-initialising stores of the freshly allocated `mi_task_struct` is emitted in a different order (the stock order is the
  original member assignment order; ours is a hill-climbed permutation that fixes the first half only). Needs the exact original statement order of ~45 member assignments.
- `reset_traced_window_show` (32 lines, 195 vs 195): load order `uid_max_value`/`develop_mode` and the resulting register numbers; the three prologue statements tried in all orders (this order is the best).
- `update_pkg_load` (24 lines, 256 vs 254): per-cluster item roll (`curr_runtime_items % 4`, `subs/csinc` wrap) scheduling; hint/permutation search could not reduce it below 24.
- `mi_user_roll_wk` (75 lines, 331 vs 334): hoisting of `adrp curr_runtime_items`/`user_zero+152` and of the `0xacbd acbd` magic constant into registers (stock rematerialises them in the loop; ours keeps them in callee-saved registers).
- `alloc_add_mi_group` (93 lines, 223 vs 230): order of the ~60 zero stores (the `cpu_prev_frame`/`cpu_curr_frame`/`cluster_*` loops are unrolled and interleaved by the machine scheduler differently);
  an explicit-store-order generator (`/tmp/gen_alloc.py`, not kept) never got below 83 lines.
- `change_lsgroup` (148 lines, 354 vs 360), `mi_group_ioctl` (212 lines, 835 vs 837): block layout / register allocation of the big switch + WARN paths. `#line 810` / `#line 769` pin the two WARN_ON bug-table lines (they match).
- `runtime_show_all` (471 lines, 459 vs 462) and `package_runtime_show` (310 lines, 336 vs 330): the long seq_printf-heavy functions; the arithmetic (`d * 10000 / window`, `max(.., 0)` percentages, top-package tracking)
  is reproduced, but instruction scheduling of the 64-bit divisions and the printf argument setup differs. Reconstructions with notes are in the source.

## Export CRCs (12 of 33 match)

genksyms CRCs depend on the full struct/typedef expansion of every parameter type. Matching ones: `get_active_mi_groups`, `get_pred_target_period`, `is_32B_user`, `num_sched_clusters`,
`oem_update_num_sched_clusters`, `period_freq_list_lock`, `pkg_enable`, `register_mi_update_global_period_f`, `register_task_group_change_update_f`, `unregister_mi_update_global_period_f`,
`unregister_sched_load_update_hook`, `unregister_task_group_change_update_f`.
The 21 mismatches all involve `struct task_struct`, `struct mi_task_struct`, `struct mi_group`, `struct mi_user_struct` or `mi_rq`. Their CRC input includes the member *names* of structs that BTF cannot give
(members of the vendor structs are named from BTF, but `task_struct` of the headers used by the stock build differs from our 6.6.144 headers: e.g. a field added by a later stable release changes every
CRC that expands `task_struct`), so these are not reachable with the 6.6.144 tree. Param names are irrelevant to genksyms; for exported variables an earlier `extern int num_sched_clusters;` without attribute
fixes the CRC (done).

## Lessons (reusable)

- Per-module ThinLTO: `mi_schedule_defconfig` with `# CONFIG_LTO_NONE is not set` + `CONFIG_LTO_CLANG_THIN=y`; promoted statics show up as `name.llvm.<hash>` (GLOBAL HIDDEN) - define non-static with
  `__asm__("name.llvm.<hash>") __attribute__((visibility("hidden")))`.  Static functions that are no longer inlined across TUs must stay `noinline` (`mi_task_world_init`).
- Bytes of deferred static functions (`proc_ops` users) follow the emission order of their first reference: define the `proc_ops` in the order the stock `create_*_proc()` references them.
- Hill-climbing the choice of `likely()/unlikely()` on `if` conditions (tool `work/scripts/perfB_mut.py`) and permuting independent statements (`work/scripts/perfB_perm.py`) fixed a dozen functions
  (`mi_sched_tick`, `create_ls_group`, `task_add_work`, `init_ravg_info`, ...). Both run on private package copies against the ~4.5 s bazel-free loop `work/scripts/perfB_q.sh`; always confirm with `re_push.sh`.
- Caching loaded fields in locals (or re-reading them) decides whether the compiler hoists loads; `-ftrivial-auto-var-init=zero` makes the declaration order of locals visible.
- memset of a sub-range of a struct: fortify complains at compile time; use `(char *)&s + offsetof(...)` as destination.
- BTF of the stock module (`btf_dump.py` with the right string base) gives exact struct layouts; unknown base IDs (`unsigned int`, `u64`, `list_head`, `spinlock_t`, `refcount_t`) are inferred from usage.
- Path pitfall: use Git bash (`Git bash`) and Windows `python` for the helper scripts; WSL bash silently uses stale server copies. Write C files with the Write/Edit tool (shell heredocs halve backslashes).
