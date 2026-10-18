# Behavioural-sufficiency audit by instruction multiset (goal-G10, 18.10.2026)

> English translation of the original (Russian) working report; numbers and verdicts are unchanged. State of the sources at the time of the audit: several modules were fixed afterwards (see the STATUS.md of each module and `multiset_audit2.md`).

A second, independent layer after `compare_ko_v2`. Sources and the package were not changed, no phone was used, only the scratch directory `<work-dir>/g10/` was used on the build server. Table of all functions: `multiset_audit.csv` in the working tree (module, function, class, evidence, group, severity, note; 2325 rows, 100 of them MODULE rows with the per-module total: 69 package modules and 31 PARTIAL ones; the CSV itself is not published).

## 1. PACKAGE (source = ours / public-built): no differences

69 unique `.ko` were checked (99 manifest files, duplicates in vendor_ramdisk/vendor_dlkm merged by sha256), 1758 functions, compared with the OS4 stock modules.

- 1756 functions are byte-identical to the stock in instruction text (registers not masked, branches compared symbolically) and in relocations.
- 2 functions differ only in layout: `millet_oem_cgroup` (`millet_can_attach`, `freezer_cancel_attach`): the replacement word `ldapr` (inline alternatives for `ldar`) lies elsewhere in .text. The multiset is equal.
- 2 functions are in another section: `perf_actuator`: `xr_fbt_dev_init` and `xr_fbt_dev_exit` are in `.text` instead of `.init.text`/`.exit.text` (init memory is not freed, behaviour is the same). Not mandatory to fix; they can be marked `__init`/`__exit` at the next rebuild.
- Not a single new `brk`/`udf`/`ud2`, not a single difference in the kCFI word in front of a function (the word in front of EVERY function is compared, not only of global/address-taken ones as in `preamble_cmp.py`), not a single difference in the set of hashes checked before indirect calls.
- Section sizes: differences only in `.modinfo`, `.rodata`/`.data`/`.bss` by units to tens of bytes (alignment, string merging, `.data.once`) and three times in `.init.eh_frame` (`debug_ext`, `fs19xx_dlkm`, `sia91xx_tuning_dlkm`): 28/128/10 FDEs match, only the number of CIEs differs (1 instead of 4/6/2). `__bug_table`, `.altinstructions`, `__jump_table`, `__ex_table` and `.text` (except `perf_actuator`) are equal in size.

**The list "must go back to stock" is EMPTY.** No package module built from our or public sources has a real difference.

## 2. Method (what was counted and what it does not see)

Scripts: `audit2.py` (main), `ctx.py`, `seq.py`, `trapreach.py`, `kattr.py`, `build_report.py` (not published; the principle is described here). For every function:
1. `identical`: same instruction text and relocations.
2. Otherwise the instruction multiset with register masking. Normalisation: stack accesses through sp, the frame, `mov R,R`, unconditional `b`, `ret`, `paciasp/autiasp`, `ldapr` are dropped; `ldp/stp` are split into two `ldr/str`; `ldur` = `ldr`; `cmp #N` + `hi/ls/gt/le` is brought to `#N+1` + `hs/lo/ge/lt`; the polarity of conditions (cbz/cbnz, b.eq/b.ne ...) is not considered; local `bl` without a relocation counts as CALL.
3. A second attempt with "inline straightening": a call of a function of the same module is replaced by that function's multiset.

Classes: `LAYOUT` (multiset equal), `INLINE` (equal after straightening), `SELECT` (only ALU/branches differ), `CONST` (immediates differ too: loop counters, comparisons), `REAL` (memory accesses not through sp, calls, barriers, atomics or traps differ), `STUB`/`MISSING`/`ADDED`, `TRAP` (extra brk/udf, which ones are added is shown), `KCFI` (the word in front of a function), `KCFICALL` (the set of hashes checked before indirect calls), `SECTION`.

It does not see: an inverted branch polarity with a mirrored layout, non-equivalence with an equal multiset (reordered operations), data values in `.data/.rodata` (that is the area of `compare_ko_v2`). Class `REAL` does not prove a bug: many entries are explained by tail merging, loop unrolling, `ldr/str` shuffling.

Built `.ko` of the non-package modules: the newest `built/<module>.ko` on the build server by time (dump_binder/miscore/rss_monitor, fault_report, mi_reclaim/sched_entity_freq, xr_qi/xr_qs, mi_game/mi_low_lat_mem, matt, synaptics_tcm2, aw882xx/sia91xx/tfa98xx, binder_gki/rsmc_driver, mibbr/ufs_ffu, cameralog/cameramsger/dcvs_main/lb/mi_damon/mist/process_monitor, qca_cld3_*). This is the state at the time of the audit: some of it was being worked on by other agents.

## 3. PARTIAL modules (stock in the package, our builds only checked)

| module | functions | identical | verdict |
|---|---|---|---|
| binder_gki | 10 | 10 | clean, no traps, no kCFI differences |
| synaptics_tcm2 | 233 | 233 | clean (function level, no kCFI/trap differences): candidate to move into the package after a full compare_ko_v2 |
| cameralog | 11 | 10 | clean (`cameralog_ioctl` layout only) |
| mist | 70 | 67 | clean (2 LAYOUT, 1 SELECT `parse_cluster_node_map`) |
| rsmc_driver | 93 | 89 | clean (4 LAYOUT); 2 kCFI words exist only in our build (`rsmc_tcxo_power_free`, `smc_free_gpio`), harmless |
| rss_monitor | 32 | 31 | clean (`init_module` LAYOUT) |
| ufs_ffu | 8 | 5 | low: `start_ffu` CONST (7 instructions, compare/csel), 2 LAYOUT |
| mibbr | 28 | 26 | low: `mibbr_main` one `ldr [x,#-8]` fewer (frame), `update_bbr_uid_list` LAYOUT |
| process_monitor | 42 | 38 | low: `detect_list_write` (mov #512), `init_D_task` (mov #-12) minor; `timeout_check` has another kCFI hash (global, no pointers in the module) |
| fault_report | 8 | 7 | low: `get_stack_info` (cmp #17 against #16 plus tbz): remainder 8, the loop bound should be re-checked |
| dump_binder | 11 | 10 | low: `extend_android_vh_binder_proc_transaction_finish` has one `_raw_spin_unlock` fewer: by call order it is a shared epilogue (described in STATUS.md), CFG not proven |
| sched_entity_freq | 36 | 33 | low, CHECKED BY HAND: the `cluster_freq_work_handler` branch jumps to a shared `spin_unlock_irqrestore` (tail merge, no lock leak); `init_module` `cmp #8` against `#0x400` is an 8 x 128-byte index step; `sched_tick_hook` moves `strb/ldr` |
| dcvs_main | 18 | 15 | medium: `mi_recalculate_freq_mhz` (EXPORT_SYMBOL, `__ksymtab`) has another kCFI hash in our build: a trap on an indirect call from outside; `mi_calculate_mon_sampling_freq` CONST (`cmp lsl #1`) |
| cameramsger | 22 | 14 | medium: another kCFI hash for `cam_sched_ioctl`, `cam_sched_w_pids`, `get_tgid_by_cmdline` (addresses lie in `.rodata/.text`, the prototype differs from the stock: trap on an indirect call); `cameramsger_read/write/release/snd_message` are REAL by `ldr/str` shuffling and stack slots (STATUS: "register allocation") |
| mi_damon | 188 | 187 | medium: `kdamond_fn` remainder 11 (`mov #4096`, `cmp #2,lsl #12`); `damon_vma_need_sample` another kCFI hash (global) |
| mi_reclaim | 30 | 26 | medium: `is_important_process` remainder 13 (`cmp #128` against `#32`, `mov` and `lsl #2`), `init_module` SELECT (`asr #37`), `event_store` LAYOUT |
| miscore | 27 | 22 | medium: `miscore_handle_link_change`: the `strnlen>=17` check leads to `fortify_panic` in the stock, to `brk #0x1` in ours (same condition, same effect); `miscore_input_hook4/6` `cset` against `mov #1` (harmless) |
| lb | 57 | 38 | medium: 7 REAL: `file_sequential_get_fileinfo` (kfree merged/reordered, same set), `mi_lb_compiler_thread` (+1 `memset`, remainder 135), `mi_lb_dealwith_collect_cmd` (inline refcount), `mi_lb_ioctl` (sanitizer calls), `real_timing_lookup_file_index` (no `fortify_panic`), `init_module`, `is_real_timing_package` |
| xr_qs | 120 | 114 | medium: `qievent_callback` (+1 call to `restore_qos_sched`, very large remainder), `qos_set_callback`, `qos_reset_callback`, `restore_qos_sched`, `set_rtg_grp` REAL; community-only family, do not load |
| mi_low_lat_mem | 110 | 82 | CRASH CANDIDATE: 25 REAL (`alloc_pages_slowpath_end_handler`, `cma_alloc_end_handler`, `dma_heap_buffer_alloc_end_handler`, `mm_direct_reclaim_end_handler`, `init_module` +`memset`, `mi_dfx_init` +`memset`), 6 other kCFI words; `mi_smart_refill_alloc` and `mi_smart_refill_extend_alloc` have their address in .text and another hash |
| mi_game | 234 | 156 | CRASH CANDIDATE: see below |
| matt | 261 | 167 | known (G1ad): `matt_clean_rtx_queue` +`brk #0x5512` (same as found in the sack loop), kCFI word of `matt_write_xmit`, the rest REAL 33; own audit, do not load |
| aw882xx_dlkm | 267 | 99 | medium: 159 REAL, 9 KCFICALL (the stock checks hashes that we do not have: devirtualisation), 3 TRAP only kCFI checks `brk #0x822x` (more indirect calls); under work, do not load |
| sia91xx_dlkm | 104 | 31 | CRASH CANDIDATE / functionality: `sipa_fw_load_work_delay` got `brk #0x800` (BUG); `sipa_pm_suspend` and `sipa_pm_resume` are 2 instructions in ours against 14 in the stock (stub); 65 REAL |
| tfa98xx_dlkm | 389 | 202 | FUNCTIONALITY: STUB (several times smaller after inline straightening) `tfa986x_specific` 769 against 1762, `tfa98xx_hw_params` 88 against 617, `tfa98xx_set_profile` 81 against 85689; 156 REAL, 39 KCFICALL, 28 kCFI words; do not load |
| xr_qi | 371 (370) | 339 | FUNCTIONALITY: STUB `android_vh_binder_proc_transaction` (1 instruction against 228), `android_vh_binder_set_priority` (1 against 253), `android_vh_percpu_rwsem_wq_add` (1 against 475), `qos_event_handler` (2 against 345); function `trace_qi_percpu_rwsem_uninherit` missing; kCFI words of `do_qos_inherit`, `undo_qos_inherit` differ. State of the first xr_qi round; later rounds are in its STATUS.md |
| qca_cld3_peach / peach_v2 / kiwi_v2 / qca6750 / wcn7750 | 18324 / 18446 / 18271 / 15880 / 17981 | 17840 / 18249 / 18074 / 15701 / 17786 | medium (97.4-99.0 percent of the functions identical): REAL 86-134 and CONST 80-335 per module (not analysed, mostly inlining/layout); `epping_stop_adapter` is a false alarm (the stock inlines `netif_tx_disable`, ours does not); `wlan_hdd_state_ctrl_param_write` of qca6750 and wcn7750 checks kCFI hashes that are not in the stock (check the pointer types); the stock checks `0x1581ffc7` in `wlan_hdd_cfg80211_scan/vendor_scan` are missing in ours (devirtualisation) |

### mi_game (build of that morning), exact functions
- `flt_irq_work`, `mi_force_update_freq`: the hash `0xc026e2eb` is checked before the indirect call, the stock has `0x4ede13bb` (wrong pointer type in the source), plus `flt_irq_work` has one `walt_sched_clock` call instead of two. A kCFI trap at call time: the most serious item in the module.
- 31 kCFI words in front of functions differ (those with an address in .text: `do_boost_work`, `game_choose_cpu_fastpath`, `mi_force_update_freq`, `update_cpu_frame_load`, `update_frame_load`; the others are global): list in the CSV, class KCFI.
- `clean_task_window`, `reset_render_info`, `update_frame_load`, `update_render_info`: `brk #0x1` added (fortify as a trap instead of `fortify_panic`, same condition).
- `init_module` is in `.init.text`, in the stock in `.text` (harmless).
- `cpu_load_show`: the stock has the loop unrolled (8 `seq_printf`), ours is a loop (checked, harmless).

## 4. What can / must be done

1. Package: nothing has to go back to stock. `perf_actuator`: mark `xr_fbt_dev_init/exit` as `__init/__exit` at the next rebuild (optional).
2. All PARTIAL modules above stay stock-only in the package; candidates for a move after a full strict comparison and a preamble check: `synaptics_tcm2` (233/233), `binder_gki` (10/10), `cameralog`, `mist`, `rsmc_driver`, `rss_monitor`.
3. Fix in the sources (priority): `mi_game` (pointer types in `flt_irq_work`/`mi_force_update_freq`, prototypes of the 31 functions with another kCFI hash), `cameramsger` and `dcvs_main` (prototypes of `cam_sched_ioctl`, `cam_sched_w_pids`, `get_tgid_by_cmdline`, `mi_recalculate_freq_mhz` for kCFI), `xr_qi` (stubs), `sia91xx` (`sipa_fw_load_work_delay`, suspend/resume), `matt_clean_rtx_queue` (brk 0x5512). Most of these were done afterwards: see `multiset_audit2.md` and the STATUS.md files.
4. Do not trust "PARTIAL, layout": repeat `audit2.py` after the fixes (1-2 minutes per module).

## 5. Not done / risks
- For `REAL` rows no line-by-line CFG analysis was done; only the functions listed above were checked by hand; the other REAL rows are marked `low/medium` by a heuristic (size of the remainder, presence of calls).
- The last built `.ko` of each module is taken, not necessarily the "final" one.
- qca_cld3_*: 202-489 non-identical functions per module (out of 16-18 thousand) were not analysed.
