# lb - PARTIAL vs the OS4 stock: all 57 functions, relocations, objects, imports+CRC (124) and modinfo match; 38 of 57 function bodies byte-identical (g4j 17.10.2026)

g4j (17.10.2026) re-reconstructed the OS4 lb from the stock .ko disassembly (BTF with START_ID 141904 + STR_BASE 2552072, see `work/a17/g4j/NOTES.md`). Real Kleaf build + compare_ko: function set OK (57),
function relocations (resolved targets) OK, global objects OK, exports (0) OK, imports + CRCs OK (124), modinfo OK (incl. all 27 `parm`/`parmtype` lines and `depends=`), kCFI preambles OK (41 functions, 0 mismatches).
Function bytes: 19 of 57 differ (before: 13 functions missing + 23 differing).

What was recovered (all from the asm, nothing was copied from a source):
- New structures: `package_info` is now 0x188 bytes (union of the file-sequential part {file_infos, file_num} and the real-timing part {file_info{file_nums,i_mtime_buffer_size,paths_buffer_size,i_mtime_buffer,file_paths}, range_nums,
  ranges_buffer_size, ranges*, reclaimed_files[3]}; owner@0x70, mm_size@0x178, is_cleared/is_real_timing/is_file_page_reclaimed@0x17c.., refcount@0x180; no package_lock any more); `lb_manager` is 0xd1d0 bytes (two package lists
  with their own mutexes, `clear_lock`, `extra_records` 2 MB buffer for 4 game packages, `nr_real_timing_apps`/`nr_file_sequential_apps` as bytes @0xd1c8/9); ioctl argument `cmd_app_info {bool is_real_timing; lb_owner}`;
  `lb_app_preread_info` 0x130 bytes with the file-sequential / real-timing union; `real_timing_file_node` and range node (48 bytes each); `g_active_package` {package, nr_pids, by_tgid, pids[5]}.
- New functions (13): `lb_file_path`, `real_timing_package_init`, `real_timing_lookup_file_index`, `dex_files_for_bitmap_record`, `real_timing_file_range_cmp`, `file_sequential_get_fileinfo` (the old inline helper became a function),
  `file_sequential_free_one_fileinfo`, `mi_lb_remove_package_info_from_list`, `is_real_timing_package`, and the four vendor hooks `adjust_readahead`, `adjust_readaround`, `mi_lb_skip_async_mmap_readahead`, `should_skip_fault_around`
  (registered in init_module for android_vh_ra_tuning_max_page / tune_mmap_readaround / do_async_mmap_readahead / should_fault_around, failures only logged).
- Rewritten for OS4: `mi_lb_compiler_thread` (the real-timing compile pipeline inlined: range list per file, list_sort + merge with g_max_gap_range/g_max_merge_range, ranges/paths/i_mtime arrays), `mi_lb_dealwith_collect_cmd` (ABORT no longer handled, RenderThread check,
  extra 2 MB buffer for com.tencent.jkchess/KiHan/brawlstars/msgame), preread size/info (real-timing branch), clear_cmd (manager clear_lock), destroy, get/add/oldest package (list selected by is_real_timing), lb_collect_show, mi_lb_ioctl
  (cmd_app_info, is_real_timing_package(), LB_GET_VERSION 0x80044c14, no get_app_names/debug_owner cases, pte_clean not called), init_module/chrdev/hooks, module parameters (8 new, stock defaults taken from .data).
- Removed OS4-dropped code: `mi_lb_dealwith_get_app_names`, `free_one_fileinfo` (now file_sequential_free_one_fileinfo), debug accounting (total/app_startup_bytes, hit_rate).
- Hacks: lock-key section emulation as before; `lb_list_del()` (always-inline copy of list_del, because clang left an out-of-line `list_del` in `file_sequential_get_fileinfo`).

Byte-identical (38): pagecache_collect, hooks adjust_*/skip_async/should_skip, package_info_put/do_destroy, init_manager/exit_manager, clear_cmd, dbg_init, lb_cold_exit, free_one_fileinfo, real_timing_file_range_cmp, lb_file_path, ... and all hot functions except the ones below.
Still differing (ThinLTO emulation word diff, g4j q.sh): `mi_lb_compiler_thread` 966 (largest; the real-timing section is structurally right, layout/allocation differs), `mi_lb_dealwith_collect_cmd` 366 (cold-block layout of the get_pid_task/RenderThread
error paths, one stp vs str prologue), `file_sequential_get_fileinfo` 275, `init_module` 223, `mi_lb_ioctl` 183 (jump table / get_user layout), `real_timing_lookup_file_index` 124, `mps_hot_pte_range` 74 (unchanged from before),
`dex_files_for_bitmap_record` 41, `mi_lb_dealwith_preread_size_cmd` 2 (registers only), `mi_lb_dealwith_preread_info_cmd` 37 (stock keeps info+0x130 and isz+0x130 in registers), `mi_lb_dealwith_pte_preread_cmd` 27, small ones (is_real_timing_package: shrink-wrapped null check at the top in stock;
list functions: register allocation only).
NOTE for a rebuild into a ROM: values of module parameters now follow the OS4 .data (lb_debug=3, lb_pte_fadvise_enable=0 ...), the old ones were HyperOS 3 values.

## History (HyperOS 3 based rounds)

g4d (17.10.2026): not advanced. ThinLTO emulation vs the OS4 stock (`work/a17/g4d/q.sh lb`): 3 of the 13 new functions are listed MISSING plus `mi_lb_remove_package_info_from_list`, `mi_lb_skip_async_mmap_readahead`, `adjust_readahead`, `adjust_readaround`, `should_skip_fault_around`, `file_sequential_free_one_fileinfo` (about 3.7 KB of new code: sizes `file_sequential_get_fileinfo` 0x594, `real_timing_lookup_file_index` 0x25c, `lb_file_path` 0x1f8, `dex_files_for_bitmap_record` 0x174, `mi_lb_remove_package_info_from_list` 0x16c, `adjust_readaround` 0x16c, `adjust_readahead` 0x150, `mi_lb_skip_async_mmap_readahead` 0x134, `should_skip_fault_around` 0x130, `is_real_timing_package` 0xc8, `real_timing_package_init` 0x120, `file_sequential_free_one_fileinfo` 0x9c, `real_timing_file_range_cmp` 0x40) and 19 shared functions with large diffs (`mi_lb_dealwith_collect_cmd` 446 words, `init_module` 258, `mi_lb_dealwith_preread_info_cmd` 156, `mi_lb_init_manager` 135, `package_info_do_destroy` 135, `lb_collect_show` 125, `mi_lb_add_package_info` 112 ...). Needs a full re-recovery of the real-timing / file-sequential preread features (4 TUs).


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
