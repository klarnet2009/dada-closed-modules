# mi_schedule - PARTIAL (90 of 100 functions) vs OS3 only; no OS4 counterpart (module vanished, g5 check 17.10.2026)

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
