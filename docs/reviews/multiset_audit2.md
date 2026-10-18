# Multiset audit, part 2 (goal-G1aj, 18.10.2026)

> English translation of the original (Russian) working report; numbers and verdicts are unchanged.

Method: ThinLTO emulation (`q.sh <module>`) + `ob.sh` (instruction multiset of the stock against our build, registers masked; `MOD=<module> MIN=n V=1`) with the tools in `tools/rounds/` (build-server scratch directory not published). Then the real Kleaf build + `compare_ko_v2` + `preamble_cmp` (`kb.sh`). In addition to G10 (`multiset_audit.md`): a list of memory accesses with offsets (`ldr/str [x,#off]` that exist only in the stock or only in ours), magic constants (`movk`), signedness (`smull/umulh/sxtw`), call sets, and reading functions line by line against the stock disassembly.

Pitfalls of the method: `ob.sh` counts local calls of the stock (`.text+0x...`) as noise while named calls in ours are not, so `+N CALL name` is almost always an artefact; offsets from different base registers (`[x19,#0xa48]` against `[x8,#0xa68]`) are an artefact too. For `fault_report` the emulation without `DEFS=-DCONFIG_MIEV=1` loses the whole `report_work_handler` (68 against 45 instructions): a false alarm, it is identical in Kleaf.

## Real differences (not register layout)

| module | function | what it was | fixed |
|---|---|---|---|
| mi_game | game_lb_control | the "in the queue too long" test read `p->on_rq` (+0x80), the stock reads `p->se.on_rq` (+0x108; offsetof verified by compiling with our headers) | yes (`migt_a.h`) |
| mi_game | uclamp_frame_load (trace) | in `trace_flt_uclamp_frame_load_mark` the arguments are `(period, d10k, ...)`, in the stock `(d10k, period, ...)`: the fields `need_target_delta` and `prev_frame_period` were swapped | yes (`flt.c`) |
| mi_low_lat_mem | sys_mem_data_dmabuf_init | `kmalloc_array(n,8,GFP_KERNEL)` (0xcc0, not zeroed) instead of the stock `__kmalloc(n*8, GFP_KERNEL\|__GFP_ZERO)` (0xdc0, no overflow check): when `kzalloc(12)` failed a garbage pointer stayed in the array and `sys_mem_data_dmabuf_free` passed it to `kfree` | yes, 78 -> 0 instructions, the function is byte-identical in Kleaf |
| mi_damon | kdamond_fn (inlined damon_split_regions_of) | split threshold `2 * DAMON_MIN_REGION` (constant 8 KiB), in the stock `2 * damon_min_vma` (global variable, 32 bits, default 0x8000, i.e. only regions > 64 KiB are split) | yes (`core.c`) |
| mi_low_lat_mem | pre_compact_time_show / pre_compact_success_rate_show / scene_alloc_info_show_common | (structure rather than behaviour) counters were read 3 times instead of 2 (ldar), `len` as int instead of ssize_t, signed indices, raw offsets instead of typed arrays | yes: 70 -> 0, 54 -> 6, 75 -> 42 |
| mi_game | migt_ioctl | `% 10` signed (smull 0x66666667), in the stock unsigned (umull 0xcccccccd) | yes, 160 -> 150 |

## Read, NO behavioural differences (layout remains)

- mi_game: uclamp_frame_load, flt_cal_freq, update_frame_load, adjust_frame_start_slowpath, xr_user_roll_wk, the rest of game_lb_control, update_pkg_load (by diff), reset/update_render_info (the `render_info_s` layout matches: last_update +0x10, name +0x18, pid +0xc, seq +0, lock +4, valid +8).
- mi_low_lat_mem: cma_alloc_end_handler completely; dma/direct_reclaim/slowpath by diff. The extra 16 bytes of the lower frame (0x200 against 0x1f0) are two extra spill slots for printk/snprintf calls with 20 arguments; the stock checks `scene<0` as `cmp -65535, w27 lsl 16` after `csinv`, we as `tbnz #15` after the same `csinv`.
- sched_entity_freq (cluster_freq_work_handler 18, sched_tick_hook 14, init_module 4): confirmed by hand in G10 (tail merge, index step 8 x 128 bytes).
- mi_reclaim (is_important_process 23, mi_alloc_pages_reclaim_bypass 8, init_module 8, the rest call noise): `cmp #0x20` against `#0x80` is a counter of 32 elements against an offset of 32 x 4 bytes; `asr #37` against `lsr #32` is a division by a constant.
- lb (mi_lb_compiler_thread 111, mi_lb_dealwith_collect_cmd 29, mi_lb_ioctl 28, real_timing_lookup_file_index 18, init_module 19): in `mi_lb_dealwith_collect_cmd` the stock has two `__put_task_struct` on two branches (after the `strcmp` of comm), ours has one shared tail: no leak; `mi_lb_ioctl`: the stock has 17 `_printk` calls with one format, ours 16 (merged). NOT read line by line: `mi_lb_compiler_thread` (917 instructions) and `real_timing_lookup_file_index` (the stock has `fortify_panic`, ours `brk`).
- fault_report (get_stack_info 15): described in STATUS.md (slot search loop, `cmp #17` against `#16`); ufs_ffu start_ffu 26: the stock has a bound check `min(ptr, 1<<63) == min(ptr, len-1)` around the crc loop (a `brk` trap), ours has another form; mibbr (mibbr_main 10, update_bbr_uid_list 4), dump_binder (1), process_monitor (detect_list_write 6), dcvs_main (mi_calculate_mon_sampling_freq 6), miscore (hook4/6 10, handle_link_change 22), cameramsger (cam_sched_w_pids 90, snd_message 136, release 16, write 17, read 9, xmi_rvh_select_task_rq_fair 17): not analysed in volume, only constants and call lists were looked at, nothing suspicious beyond what G10 already describes.
- mist: the emulation did not build in this run (no `cc.flags`/include in the shared `pb_ccl.sh`); not checked.

## Not done / risk

- Stack frames, spill slots and block order were not touched (rule: 3 iterations per function).
- `stats_history_show` (92), `predo_store_call` (89), `cma_pool_shrink_count` (43), `mi_dfx_init` (21) and three end handlers (96/94/81) remain.
- The fixes were checked only by emulation and by a byte-wise Kleaf build; nothing was run on a phone, and mi_game and mi_low_lat_mem stay DO NOT LOAD until reviewed.


# Part 3: global variable versus constant (goal-G1ak, 18.10.2026)

> English translation of the original (Russian) working report; numbers and verdicts are unchanged. This part is later than part 2 above: items listed there as "remain" (`cma_pool_shrink_count`, `stats_history_show`, `predo_store_call`) were partly fixed here.

Tool `tools/rounds/ldst.py <module> [built.o|.ko] [minimum]`: for every function it counts the accesses to global scalars (relocations `LDST*_ABS_LO12_NC`); stock targets of the form `.bss+0x598c` are turned into `symbol+offset` through the local symbol table of the stock (`llvm-nm --format=sysv`). It prints the functions where the stock has more such reads than ours (where ours may have an immediate). Pitfall: tail duplication in the stock (one block in two branches) gives an extra read without any constant; every case was checked in the disassembly.

| module | functions | with a stock read that ours does not have | result |
|---|---|---|---|
| mi_damon | 194 | 0 | the only case, `damon_min_vma`, was fixed by G1aj |
| mi_reclaim, mibbr, dump_binder, process_monitor, dcvs_main, ufs_ffu, mist | 36/34/17/48/24/14/76 | 0 | clean |
| mi_game | 239 | 8 | tail duplication (`game_lb_control`: `vip_prefer_cluster` 6 times in the stock, the block is duplicated for two branches of `force_forbidden_walt_lb`), a field read `ceiling_users+k` in `migt_ioctl`, inlining; no constants instead of globals |
| lb | 63 | 3 | tail duplicates (`is_real_timing_package`, `init_module`, `collect_cmd`) |
| sched_entity_freq | 42 | 1 | `sched_tick_hook`: two `strb #1,[cl,#0x24]` and two reads of `config.mid/min_freq` on two branches in the stock, ours has a common tail (by the listing) |
| miscore | 33 | 1 | `miscore_handle_link_change`: the stock unrolls the search over 3 `link_info`, ours is a loop |
| matt | 267 | 2 | `bbr_init` (ours calls `bbr_init_pacing_rate_from_rtt`, the stock inlines it), `adaptive_bit_unit_to_str` (the stock has no branches, `sdiv`; ours `udiv`) |
| mi_low_lat_mem | 116 | 1 | `init_module` (`predo_worker_task`) |
| cameramsger | 27 | 1 | `cameramsger_release`: `debug()` is evaluated twice |
| qca_cld3_peach_v2 (whole module, g4u build against the stock) | 18451 | 0 | the multiset of global reads is IDENTICAL in all 18451 functions (only the numbering `__last_ticks.N` differs), so on the hot paths (`ce_*`, `dp_*`, `hif_*`, `wma_*`) there are no constants instead of globals either |

Summary of item 3: no new cases of the `damon_min_vma` type were found.

## Real differences found by reading (G1ak)

| module | function | what was wrong | fixed |
|---|---|---|---|
| mi_low_lat_mem | predo_store_call | the trace string `I\|%d\|i:%d avg:%d refill_duration` got `current->pid`; the stock reads +0x61c = `tgid` (like every other `I\|%d\|` of the module) | yes |
| cameramsger | snd_message | the stock returns -1010 at once when `len < 8` (before the loop and without a debug print); ours let messages of length 5..7 reach the parser | yes |
| mi_low_lat_mem | cma_pool_shrink_count | ours walked 4 zones unrolled, without the `contig_page_data.node_id == 0` filter and without the 5th zone; the stock walks through `zone->zone_pgdat` with the filter | yes (`llm_for_each_zone`, 43 -> 4) |

Codegen: `stats_history_show` 92 -> 11, `predo_store_call` 89 -> 37 (a common stack slot for `buf[128]` and the wait entry: a separate block), `snd_message` 136 -> 54 (order of the local declarations, index `j`), `mi_low_lat_mem` in total 767 -> 595 instructions, `cameramsger` about 295 -> 211. Kleaf: no additional byte-identical functions (25 of 111 differ in `mi_low_lat_mem`, 8 of 22 in `cameramsger`), everything else PASS as before.

Read without behavioural differences: `cam_sched_w_pids` (tail), `lb::real_timing_lookup_file_index`, `lb::mi_lb_compiler_thread` (signed indexes), `mi_game::migt_hardwork`, `process_monitor::detect_list_write` (the stock itself does `kzalloc(512, count)`: flags = count, a vendor bug, kept), `mibbr`, `dump_binder`, `dcvs_main`, `miscore` (by multiset). mist: the emulation now builds (`mist/STATUS.md`): 3 of 76 functions differ, `mist_frame_load_history_update` is identical.

## Not done

- `mi_dfx_init` (21), three end handlers (96/94/81: the stock indexes by a signed scene, `smaddl` against `umaddl`), the rest of `predo_store_call` (registers), the sort loop of `stats_history_show`.
- `mi_game::migt_hardwork` and `mist` were not read line by line in full (multiset + constants).
- The phone was not touched; `mi_game` and `mi_low_lat_mem` stay DO NOT LOAD until reviewed.
