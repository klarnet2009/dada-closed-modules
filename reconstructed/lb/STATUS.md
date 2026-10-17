# lb - PARTIAL vs the OS4 stock: OS4 grew new code (13 functions + vendor hooks) that our HyperOS 3 based source lacks (g4 re-check 17.10.2026); vs HyperOS 3 it was 42/46

Re-checked against `<work-dir>` (real Kleaf build of the unchanged source): the module changed in OS4.
- function set: OS4 has 13 functions that we do not have: `adjust_readahead`, `adjust_readaround`, `dex_files_for_bitmap_record`, `file_sequential_free_one_fileinfo`, `file_sequential_get_fileinfo`, `is_real_timing_package`,
  `lb_file_path`, `mi_lb_remove_package_info_from_list`, `mi_lb_skip_async_mmap_readahead`, `real_timing_file_range_cmp`, `real_timing_lookup_file_index`, `real_timing_package_init`, `should_skip_fault_around`;
  we have 2 that OS4 dropped (`free_one_fileinfo`, `mi_lb_dealwith_get_app_names`).
- new imports (OS4): vendor hooks `android_vh_tune_mmap_readaround`, `android_vh_do_async_mmap_readahead`, `android_vh_ra_tuning_max_page`, `android_vh_should_fault_around` (readahead / fault-around tuning), `list_sort`, `strstr`,
  `d_path`, `dentry_path_raw`, `prepare_to_wait_event`, `init_wait_entry`, `device_destroy`; dropped: `autoremove_wake_function`, `prepare_to_wait`, `strnlen`, `__bitmap_weight`.
- new global objects (stock names): `g_real_timing_supported`, `g_skip_background_readahead_enable`, `g_background_faultaround_disable`, `g_max_real_timing_apps`, `g_max_file_sequential_apps`, `g_max_gap_range`, `g_max_merge_range`.
- 23 of the remaining 33 shared functions differ in bytes (the new code changed shared structures), relocation lists are identical for the rest.
So lb in OS4 needs a re-recovery of the real-timing / file-sequential preread features (about 13 functions, 4 TUs); not started. Nothing of this was recoverable by tweaking the old source.

## Earlier rounds (HyperOS 3 binary as reference)
Build: Kleaf ddk_module, `lb_defconfig` = ThinLTO (`# CONFIG_LTO_NONE is not set` + `CONFIG_LTO_CLANG_THIN=y`), 4 TUs
(`lb_main.c lb_cold.c lb_hot.c lb_pkg.c`) because stock is multi-TU ThinLTO (cross-file return values are not folded,
only `noinline` cross-file functions stay un-imported). `-ffunction-sections -fdata-sections` as in stock.
No stubs needed (all 117 imports are kernel symbols).

compare_ko (real Bazel build, `work/scripts/perfC/pb.sh lb`):
- function set: OK (46)
- function relocations (resolved targets): OK, all identical
- global objects (name,size): OK
- exports + CRC: OK (0 exports); imports + CRC (__versions): OK (117)
- modinfo (incl. MODULE_IMPORT_NS VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver): OK
- function bytes: DIFF in 4 of 46 functions (everything else byte-identical)

## The 4 remaining functions (instruction stream is the same, only allocation/scheduling differs)
| function | ck diff lines | what differs |
|---|---|---|
| mi_lb_compiler_thread | 4 (stock 665 insns, built 665) | new-info path: stock `ldp x9,x10,[x8,#0x68]; stp x9,x10,[x0,#0x30]` (tv_sec in x9), ours has x10,x9. Statement-order / field-wise / memcpy / `inode` local variants all gave >= 4 lines. |
| mi_lb_dealwith_pte_preread_cmd | 52 (592 / 592) | register-normalised diff is 0; pure permutation inside the seg_range loop (stock hot=x23 vma=x21 off=x23 len=x21; ours hot=x25 vma=x23 off=x21 len=x23). ~150 compiles tried (hot reload placement, READ_ONCE(hot), ...). |
| mps_hot_pte_range | 56 (159 / 159) | layout only: stock puts the `snap_anon += nr` block right after the loop preheader, ours after the loop; first compound_head() loads the head word into x9 and does `mov x8,x0` before `tbnz w9,#0`, ours loads into x8. Arm order, early return, likely/unlikely and goto forms did not move it. |
| init_module | 81 (175 / 176) | stock keeps x21=&g_lb_data, x20=adrp(.bss+8) and the error pointer as a phi after partial redundancy elimination (class value in x19 on the no-print edge, `ldr x19,[x20]` reloaded after the shared printk block). Tried cls local vs direct field, assignment orders, goto-fail, int vs long err. |

## Source forms that mattered (reusable)
- `LB_E(line,...)` macros with explicit `__LINE__` constants; helper function names matter because `__func__` ends up in rodata
  (a wrong helper name shows up only as a string relocation difference, so check the `__func__` hints first and split inlined bodies
  into the helpers the strings suggest: `mps_rec_prepage_seg_range`, `mps_rec_prepage`, `launch_reset_fileinfo`, `mi_lb_get_fileinfo`).
- Lock-key sections emulate stock key merging (`.bss.lb_keys_a/b`, first key symbol named like stock); `no_sanitize("bounds")` on mi_lb_ioctl.
- A helper returning an int with the pointer in an out-param (`mi_lb_get_fileinfo(.., &info)`) removes the null checks a pointer-returning
  helper leaves on the success path. A non-always_inline `static inline` helper is simplified by LLVM before inlining, so moving a loop
  into a helper that returns `k < n` keeps stock's flag-in-loop form; passing `index` and computing `index << 21` inside the callee gave stock's `ubfiz`.
- Struct locals initialised at the declaration (`struct mm_walk_ops ops = {...}`) give the stock memcpy-from-rodata template.
- Statement order inside a function changes constant materialisation/register numbering: assign a pointer local AFTER a store instead of in its
  declaration (collect_cmd); put the bound first in `n < N && n == nr` and add `if (n == N) goto out;` (pagecache_collect).
- FORTIFY size check kept by testing the unsigned `n > 255` and using strncpy's return value (`dst = strncpy(..); dst[n] = 0`).
- `int huge = pmd_trans_huge(*pmd);` (int, not bool) forces the branchless speculated form; `spinlock_t *ptl = NULL;` fixes the frame size;
  `rec->f_bc++; rec->f_b8++;` in that order avoids a SimplifyCFG sink with a pointer phi.
- Moving the xarray drain into `static inline mps_free_tree(hot, xa)` and calling it twice matched mps_delayed_free_rec (for/i form never did).

Tools: `work/scripts/perfC/ck.sh lb thin [func]`, `pb.sh lb`, `vsearch.py`, `vsn.py` (register-normalised score), `nd.sh <module> <func>`
(register-stripped diff: tells structure from allocation), `dis_ours.sh`.
