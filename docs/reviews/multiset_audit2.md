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
