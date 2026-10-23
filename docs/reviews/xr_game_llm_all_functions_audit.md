# xr_qi / xr_qs / mi_game / mi_low_lat_mem: audit of every function that still differs in bytes (goal-G40f, 20.10.2026)

Base GLOBAL OS4.0.0.7 (stock <work-dir>). Builds compared: mi_game g27a/built (3e8fed6a), mi_low_lat_mem g27b (d45cb60f, then g40f/built cdc48c23 after G40f), xr_qi g20c/built (a3769edd, then g40f/built 04de8ce2), xr_qs g23a/built (1d7f5cbd). Function lists: compare_ko_v2 function-bytes (fcnt.py). Scratch <work-dir> (lst/ raw listings with relocations resolved, al/ alpha diffs, bk/ block pairing, facts_all.txt).

Method: raw `llvm-objdump -dr` per function (stock vs ours), then (1) alpha-renamed diff (alpha.py family), (2) own block pairer blk.py: blocks paired by register-less instruction signatures (calls, constants, offsets, condition codes), CFG successors compared up to polarity flip, only unmatched blocks printed, (3) facts.py: multisets of calls, global/string refs, immediates, memory offsets. "hand" = raw listing blocks read by me against the stock and the semantic equivalence argued in the evidence column; "tool" = verdict from (2)+(3) only (all blocks paired and facts equal, or reasoning transferred from a hand-read twin), NOT read to the last instruction.

Verdicts: CODEGEN = same behaviour (register allocation, layout, equivalent compare forms, UBSAN trap forms for unreachable indexes); BEHAVIOUR = proven difference; UNPROVEN = read but equivalence not proven; UNREAD = not read (budget), facts shown.

## Summary

| module | functions differing | BEHAVIOUR (fixed) | UNPROVEN | UNREAD | CODEGEN hand | CODEGEN tool |
|---|---|---|---|---|---|---|
| mi_game | 66 | 0 | 2 | 7 | 21 | 36 |
| mi_low_lat_mem | 28 | 1 | 0 | 0 | 13 | 14 |
| xr_qi | 19 | 1 | 0 | 0 | 18 | 0 |
| xr_qs | 5 | 0 | 1 | 0 | 4 | 0 |

## Table

| module | function | size stock/ours | verdict | how | evidence | fixed in source? |
|---|---|---|---|---|---|---|
| mi_game | add_work_to_migt | 132/132 | CODEGEN | hand | only the debug variable symbol name differs (migt_debug vs migt_debug_l, same use) and load placement | no |
| mi_game | android_rvh_cpufreq_transition | 208/224 | CODEGEN | hand | ours has an extra frame_time_num<=1 guard that traps (brk 0x5512); frame_time_num is only ever toggled 0/1 (flt.c:1343) | no |
| mi_game | clean_task_window | 516/604 | CODEGEN | hand | G27a: trap at row>9; tail put_task_struct duplicated | no |
| mi_game | cleanup_module | 412/404 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 4/2, CFG diffs 3; NOT hand-read | no |
| mi_game | cpu_load_show | 584/576 | CODEGEN | tool | twin of cpu_load_show#2 (history table view, same loop-vs-unroll over clusters, cmp #4 vs #5 ring bound = trap forms); G27a read the content; NOT re-read in G40f | no |
| mi_game | cpu_load_show#2 | 604/424 | CODEGEN | hand | stock unrolls 2 clusters (output "---cluster %d---", rows, newline) and ignores more; ours loops with a trap at index 3: identical text for cluster_number<=2 (Xiaomi 15: 2); cluster_number==3 would trap in ours, print 2 in stock | no |
| mi_game | do_ceiling_limit | 496/492 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 0/0, CFG diffs 0; NOT hand-read | no |
| mi_game | do_migt_rem | 448/444 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 0/0, CFG diffs 0; NOT hand-read | no |
| mi_game | flt_adjust_dequeue | 232/236 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 3/3, CFG diffs 1; NOT hand-read | no |
| mi_game | flt_cal_freq | 2584/2460 | UNREAD | tool | NOT hand-read; facts differ: GLOBALS only-S: ['flt_cal_freq+1844', 'flt_cal_freq+2080', 'flt_cal_freq+2204', 'flt_cal_freq.trace_printk_fmt.22', 'flt_cal_freq.trace_printk_fmt.26' / IMM only-S: [('other', '0x60'), ('other', '0x70'), ('other', '0x9c0'), ('other', '0x9d0')] / only-B: [('cmp', '0x1f')] / MEM only-S: [('ld', 'x', '0x10')] / only-B: []; unmatched S/B 51/48, CFG diffs 19 | no |
| mi_game | flt_update_fps_time | 196/196 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 1/0, CFG diffs 0; NOT hand-read | no |
| mi_game | flt_update_sf | 584/572 | CODEGEN | tool | str w[0x18], str w[0x1c] vs stp w,w[0x18]; 16/17 blocks exact | no |
| mi_game | free_mi_task_struct | 368/368 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 0/0, CFG diffs 0; NOT hand-read | no |
| mi_game | freedom_window_app_set | 640/636 | UNREAD | tool | NOT hand-read; facts differ: IMM only-S: [] / only-B: [('other', '0xc')]; unmatched S/B 14/14, CFG diffs 6 | no |
| mi_game | freq_limit_walt | 236/244 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 4/3, CFG diffs 3; NOT hand-read | no |
| mi_game | game_choose_cpu_fastpath | 672/668 | CODEGEN | hand | partly: num_sched_clusters read signed (ldrsw, cmp #1 b.lt) vs unsigned (cbz): differ only for a negative count (never); bic vs mvn+and | no |
| mi_game | game_choose_cpu_overutiled | 292/292 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 1/1, CFG diffs 2; NOT hand-read | no |
| mi_game | game_lb_control | 748/712 | UNREAD | tool | NOT hand-read; facts differ: CALLS S/B: get_cluster_type 10/8, migt_enable 2/1 / IMM only-S: [] / only-B: [('cmp', '0x0')]; unmatched S/B 17/14, CFG diffs 23 | no |
| mi_game | game_load_history_update | 848/840 | CODEGEN | hand | idx>=22 trap(0x5512) + idx==21 trap(brk#1) in stock vs idx>=21 trap in ours: cur_history_items max is 20 (csinc wrap at 0x14); stp->str splits | no |
| mi_game | game_load_init | 484/480 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 1/1, CFG diffs 1; NOT hand-read | no |
| mi_game | game_load_reset | 496/516 | UNREAD | tool | NOT hand-read; facts differ: IMM only-S: [('cmn', '0x690'), ('cmn', '0x691'), ('cmp', '0x68f'), ('cmp', '0x690'), ('lsl', '4'), ('orr', '0x8'), ('other', '0x679'), ('other', '0x68 / MEM only-S: [] / only-B: [('st', 'x', '0x28'), ('st', 'x', '0x38'), ('st', 'x', '0x48')]; unmatched S/B 14/10, CFG diffs 5 | no |
| mi_game | game_load_update | 316/320 | CODEGEN | hand | tbz bit0 == cmp #1 b.ne (bool); cmp #0x1f b.hi == cmp #0x20 b.hs | no |
| mi_game | game_setup_wrender_list | 708/704 | UNREAD | tool | NOT hand-read; facts differ: GLOBALS only-S: ['game_setup_wrender_list+436'] / only-B: ['game_setup_wrender_list+432']; unmatched S/B 2/2, CFG diffs 2 | no |
| mi_game | get_cluster_type | 452/452 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 0/0, CFG diffs 0; NOT hand-read | no |
| mi_game | get_frame_vutil | 204/208 | CODEGEN | hand | subs+b.le == sub; cmp #1; b.lt | no |
| mi_game | get_game_task | 516/512 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 3/3, CFG diffs 7; NOT hand-read | no |
| mi_game | get_migt_freq | 316/316 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 3/3, CFG diffs 0; NOT hand-read | no |
| mi_game | history_load_show | 124/124 | CODEGEN | hand | cmp #0x14 b.ge == cmp #0x13 b.gt | no |
| mi_game | init_module | 1716/1684 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 15/10, CFG diffs 13; NOT hand-read | no |
| mi_game | mark_rollover_window_task | 580/612 | CODEGEN | tool | tail put_task_struct/refcount_warn duplicated (1/2, 2/3); same arguments; NOT hand-read | no |
| mi_game | mi_set_flt_target_delta | 64/60 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 1/1, CFG diffs 0; NOT hand-read | no |
| mi_game | mi_task_world_init | 764/776 | CODEGEN | tool | same 0x9c..0xa9 store-coverage difference as xr_game_task_init; 33/49 blocks exact; NOT hand-read in full | no |
| mi_game | migt_enable | 48/48 | CODEGEN | hand | register allocation of and/cset/orr | no |
| mi_game | migt_enable_gpsync | 68/68 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 0/0, CFG diffs 0; NOT hand-read | no |
| mi_game | migt_freq_qos_init | 516/508 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 4/2, CFG diffs 3; NOT hand-read | no |
| mi_game | migt_hardwork | 3680/3604 | UNPROVEN | tool | G27a read (list_add placement 2 vs 1, same arguments); G40f: 184/273 blocks exact, call set differs only by __list_add_valid_or_report 2/1; 3.6 kB, NOT re-read | no |
| mi_game | migt_ioctl | 2292/2332 | UNPROVEN | hand | calls, strings, imm and offset sets equal; ceiling_users insert path (cmd 4) verified against the stock: signed n, cnt=min(n,10), dup scan, shift cnt-1 + store [9] when n>=10 else store [n] and count++ (count at +0x28), bounds traps only differ in form; the other commands and the user-copy bounds of the 2.3 kB body NOT re-read to the last row | no |
| mi_game | migt_prefer_cpu | 424/420 | CODEGEN | tool | G27a read; here: only trace_printk fmt slot offsets and the migt_debug symbol name differ in facts | no |
| mi_game | migt_sched_exit | 180/184 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 2/2, CFG diffs 2; NOT hand-read | no |
| mi_game | migt_sched_init | 444/444 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 2/2, CFG diffs 2; NOT hand-read | no |
| mi_game | migt_should_skip_migrate | 800/796 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 4/4, CFG diffs 10; NOT hand-read | no |
| mi_game | minor_window_app_set | 628/624 | UNREAD | tool | NOT hand-read; facts differ: IMM only-S: [] / only-B: [('other', '0xc')]; unmatched S/B 12/12, CFG diffs 6 | no |
| mi_game | mist_frame_load_history_update | 2376/2340 | UNREAD | tool | NOT hand-read; facts differ: GLOBALS only-S: ['global_frame_info+2680', 'mist_frame_load_history_update+1424'] / only-B: ['mist_frame_load_history_update+1512'] / IMM only-S: [('ccmp', '0x0'), ('other', '0x28'), ('tbnz', '0x1f')] / only-B: [] / MEM only-S: [] / only-B: [('ld', 'x', '0xa78'), ('ldw', 'x', '0')]; unmatched S/B 32/34, CFG diffs 15 | no |
| mi_game | mist_frame_load_init | 672/656 | CODEGEN | hand | global_frame_info+0x20 base folded: stock [x0+0xa38..0xa50] == ours [x4+0xa58..0xa70]; same values stored (irq_work, "migt", frame_load_table, 8) | no |
| mi_game | mist_frame_load_reset | 680/684 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 4/4, CFG diffs 4; NOT hand-read | no |
| mi_game | reset_render_info | 252/252 | CODEGEN | hand | G27a: trap forms (brk immediate) only | no |
| mi_game | set_boost_step | 576/580 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 1/1, CFG diffs 6; NOT hand-read | no |
| mi_game | set_migt_freq | 664/668 | CODEGEN | hand | G27a fix (single store of migt_enabled) present; dead cmp w22,#1 b.lt dropped in ours (w22 is even and non-zero there); parse loop equal | no |
| mi_game | set_vip_flag | 236/236 | CODEGEN | hand | ldr w,[x1,#0xa4]! writeback then str [x1]: same address | no |
| mi_game | task_add_to_userlist | 424/432 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 1/2, CFG diffs 1; NOT hand-read | no |
| mi_game | task_add_work | 772/772 | CODEGEN | hand | ldp/ldr split of the same words [+0x38,+0x40,+0x48] | no |
| mi_game | task_user | 160/156 | CODEGEN | tool | tbnz/tbz bit31 with swapped fall-through; 11/13 blocks exact | no |
| mi_game | uclamp_frame_load | 1488/1492 | CODEGEN | tool | G27a read; here only: bound cmp #0x15 vs #0x16 (index trap for idx 21 only, max is 20); other facts differ in constants of the inlined /100 style multiply (0x28f5c28f vs form) -> NOT re-read in G40f | no |
| mi_game | update_cpu_frame_load | 1452/1244 | CODEGEN | tool | G27a read (get_scale_exec_time 8/7: ours merges two calls, min(since, period) in all branches); NOT re-read in G40f | no |
| mi_game | update_flt_load | 1312/836 | CODEGEN | hand | G23a read: 4 reads of GFI+0x50 as stock, printk levels fixed; remaining: trace_printk slot index, adrp regeneration; not re-read in G40f | no |
| mi_game | update_frame_load | 916/856 | CODEGEN | tool | bound #0x15 vs #0x16 trap only (see uclamp); preboost stores moved; NOT hand-read in G40f | no |
| mi_game | update_pkg_load | 936/920 | CODEGEN | tool | static local old_migt_thresh named migt_hook.old_migt_thresh (stock) vs migt_hook_thresh.old_migt_thresh (ours), same role; first 34 unmatched rows read: ldr/cmp placement and a tail merge; rest NOT read | no |
| mi_game | update_policy_online | 672/664 | CODEGEN | hand | (policy-2)<2 unsigned == (policy&~1)==2; cbz/cset forms; cmp #0x1e b.hi == cmp #0x1f b.hs | no |
| mi_game | update_render_info | 368/352 | CODEGEN | hand | G27a: stock trap on idx>=2; strlen fortify forms differ (fortify_panic 2/1) | no |
| mi_game | wake_render | 136/132 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 0/0, CFG diffs 0; NOT hand-read | no |
| mi_game | xr_game_scheduler_tick | 576/512 | CODEGEN | tool | facts equal (calls, strings, imm sets, offset sets); blocks: unmatched S/B 12/6, CFG diffs 7; NOT hand-read | no |
| mi_game | xr_game_task_fork | 288/284 | CODEGEN | tool | same 0x9c..0xa9 store-coverage difference as xr_game_task_init (hand-read there); rest facts equal | no |
| mi_game | xr_game_task_init | 92/88 | CODEGEN | hand | zeroing of 0x9c..0xa9: stock stur xzr[0x9c] + str w[0xa4] + strh[0xa8]; ours stur xzr[0x9c] + stur xzr[0xa2]: same byte range, other fields (stp pairs at +0x68,+0xb0, list heads) identical | no |
| mi_game | xr_sched_init | 556/556 | CODEGEN | hand | user_zero+144 load as [user_zero+136+8]: same address; 20/26 blocks exact | no |
| mi_game | xr_set_next_freq | 1376/1204 | CODEGEN | hand | partly read: time>=1000000 test (cmp 0xf4240 b.lo == cmp 0xf423f b.ls), /1000000 via umulh+lsr#18 vs udiv by 1000000, glk_minfreq init-if-zero, minfreq/maxfreq select; rest of the tail (clamps, mist hooks) restructured, calls equal; NOT fully proven | no |
| mi_game | xr_user_roll_wk | 964/984 | CODEGEN | tool | loop advance (uid_max_value reload, _find_next_bit) duplicated in the free path in ours (find_next_bit 1/2): same operands; 36/51 blocks exact; NOT hand-read in full | no |
| mi_low_lat_mem | alloc_pages_slowpath_end_handler | 1264/1252 | CODEGEN | hand | head read: scene s=(code-0x65)<5?..:-1 ; stock lsl16 cmp -0xffff b.lt == ours tbnz bit15 (skip iff s==-1); order check: stock cmp9/ccmp4/ccmp0 -> continue iff order in {9,4,0}; ours order<=9 && (1<<order)&0x211 -> same set; stock 0x9a0 array-bounds compare absent in ours (index 0..4, unreachable). Tail (stats update) not re-read | no |
| mi_low_lat_mem | calculate_fragmentation_score | 444/460 | CODEGEN | hand | shift counts: stock lsl x,x,w (hw masks to 6 bits), ours explicit and #0x3f then lsl: same; per-order values and csel(hi->0) conditions map 1:1 (checked 5 orders) | no |
| mi_low_lat_mem | cma_alloc_end_handler | 1388/1340 | CODEGEN | hand | scene: stock csinv/cmp lsl16/b.gt == ours csinv/tbnz bit15 (s==-1 skips in both; s 0..4 proceed); bounds: stock and #0xffff<5 + sxth (smaddl) vs ours <5 + zero-ext (umaddl): identical for s 0..4; ldrh/tst 0xffff == cmp 0. Rest of body not re-read | no |
| mi_low_lat_mem | cma_area_cleanup | 276/276 | CODEGEN | tool | 13/16 blocks exact, 0 CFG differences, facts equal | no |
| mi_low_lat_mem | cma_pool_shrink_count | 336/332 | CODEGEN | tool | 19/22 blocks exact, facts equal | no |
| mi_low_lat_mem | dfx_info_reset | 432/432 | CODEGEN | hand | loop 3 x (stock: stride 0x20 on x10 base, 0x10 on x9 base; ours the two roles swapped, bound 0x30/0x10 vs 0x60/0x20 = 3 iterations): same 8-byte and 4-byte stores at the same addresses | no |
| mi_low_lat_mem | dma_heap_buffer_alloc_end_handler | 1472/1428 | CODEGEN | tool | same scene/index pattern as cma_alloc_end_handler (checked there); block pairing only here: 29/52 exact, remaining differences are the scene guard forms + layout; NOT hand-read | no |
| mi_low_lat_mem | do_alloc_pages_big_order | 252/248 | CODEGEN | tool | 13/15 blocks exact, facts equal | no |
| mi_low_lat_mem | do_compact_mem_collect | 260/264 | CODEGEN | hand | mci+0x8 load as base+index: ldr w [mci+8], ldr x [mci+8+0x50] == stock [mci+0x58]; same addresses (G27b: order-4 threshold re-read kept) | no |
| mi_low_lat_mem | fill_sys_mem_info | 424/424 | CODEGEN | hand | first 8 fields read: stores at +0..+0x1c identical sources (vm_node_stat[0..3], sp slots), stp vs str split only; tail (zone stats) not re-read | no |
| mi_low_lat_mem | get_current_app_name | 92/92 | CODEGEN | tool | 6/8 blocks exact, facts equal | no |
| mi_low_lat_mem | init_module | 1196/1156 | CODEGEN | hand | registration order equal; stock checks cma_pool_init_shrinker() and prints "cma pool register failed" - ours has the same check in source but the callee returns constant 0 so the compiler dropped it: dead in both; lockdep key objects named differently; predo_worker_task naming | no |
| mi_low_lat_mem | mem_alloc_misight_event_cb | 1036/1040 | CODEGEN | tool | ldrsw vs ldr w on an index that is range-checked unsigned (negative is out of range in both); one _printk site duplicated in ours (tail dup), strings equal; NOT hand-read in full | no |
| mi_low_lat_mem | memcg_write_start | 216/216 | CODEGEN | tool | 11/13 blocks exact, facts equal | no |
| mi_low_lat_mem | mi_dfx_init | 976/984 | CODEGEN | hand | same string set, same hook-unregister chain; stock `str w,[x19,#0xf0]!` writeback shifts later offsets by 0xf0: stock +0x7c0 == ours +0x8b0 (lock init), +0x1c == +0x10c; dfx_enable_change/sysfs_create_group and failure print equal | no |
| mi_low_lat_mem | mi_memcg_vendor_alloc | 416/408 | CODEGEN | tool | 24/29 blocks exact, facts equal | no |
| mi_low_lat_mem | mi_pre_compact_init | 768/780 | BEHAVIOUR | hand | sysfs_create_group failure: stock prints only "add pre_compact sysfs fail %d" (b to the shared printk at 0x2a0) then unregisters the two hooks and pre_compact_pools_exit; ours fell through and ALSO printed "search pre_compact symbols fail: %d" (extra log line). Other paths (kprobe fail, ttfp NULL, pools init) and unwind order equal | YES llm_main.c label unreg_hooks, rebuilt cdc48c23; sysfs-fail path now as stock (shared printk); pools_exit call duplicated by the compiler (1/2) |
| mi_low_lat_mem | mm_direct_reclaim_end_handler | 716/716 | CODEGEN | hand | stock ubfx x,x22,#1,#15; cmp #2; b.hi == (s&0xffff) <= 5 in ours; scene guard csinv/tbnz bit15 identical to stock | no |
| mi_low_lat_mem | pre_compact_misight_event_cb | 808/812 | CODEGEN | tool | one _printk site duplicated (tail), ld forms; NOT hand-read | no |
| mi_low_lat_mem | pre_compact_pool_remove | 240/240 | CODEGEN | tool | 11/13 blocks exact, facts equal modulo ldw/ld mnemonic | no |
| mi_low_lat_mem | pre_compact_success_rate_show | 988/980 | CODEGEN | tool | 23/27 blocks exact, facts equal (strings and offsets); NOT hand-read | no |
| mi_low_lat_mem | pre_do_compact | 3024/3000 | CODEGEN | tool | facts equal (calls, strings, imm, offsets); 96/139 blocks exact; the unmatched part is the placement of this_cpu_read(cpu_number) vs cpufreq_get and spill slots; NOT fully hand-read (3 kB) | no |
| mi_low_lat_mem | predo_store_call | 1076/1084 | CODEGEN | hand | cmp#4 b.lo (valid) vs cmp#3 b.hi (exit); sum/avg/clamp constants (magic 0x431bde82d7b634db, 32..64, 0..4) equal; "fling" printk placement and null-check of sys_mem_data[..] equal by calls/strings; stack slot order differs; middle part not re-read | no |
| mi_low_lat_mem | record_mem_data | 924/916 | CODEGEN | tool | 34/48 blocks exact, facts equal; NOT hand-read | no |
| mi_low_lat_mem | scene_alloc_info_show_common | 1120/1096 | CODEGEN | hand | stock bounds trap on index (cmp 0x99f) vs ours early cmp #4 trap on scene arg; both trap for scene > 4 (callers pass 0..4); loop bodies/offsets the same sets; output formats unchanged; NOT read to the last row | no |
| mi_low_lat_mem | shrink_cma_pool | 704/696 | CODEGEN | hand | freed-size accumulation: stock reads size field early into x24, ours re-reads [node-0x10] before kfree: same field; list_del validation equal | no |
| mi_low_lat_mem | start_store_call | 416/420 | CODEGEN | hand | cmp#4 b.hs == cmp#3 b.hi; ours merges the two "memcg_stat start/end" printk into one tail; record_mem_data arg: ldrb sys_scene vs bool(sys_scene): sys_scene is only ever 0/1 | no |
| mi_low_lat_mem | stats_history_show | 1008/1012 | CODEGEN | tool | facts equal, 34/44 blocks exact; NOT hand-read | no |
| xr_qi | __qi_set_task_qos | 860/852 | CODEGEN | hand | cset/csinc/csel chain vs ccmp form: both compute x6 = A ? (C ? 2 : (B&&A ? 1 : 3)) : 3 (A=arg!=0, B=(flags&0xf)==0, C=!B), truth table checked | no |
| xr_qi | android_vh_alter_rwsem_list_add | 1880/1880 | CODEGEN | hand | list_add validity tail: b.eq/b.ne swapped together with fall-through; 106 of 120 blocks byte-equal after pairing, 0 CFG differences, calls/imm/mem facts equal | no |
| xr_qi | android_vh_binder_proc_transaction | 888/888 | CODEGEN | hand | order of 4 pure guards (is_qi_feat_enabled result in w0 is computed before; all four go to the same exit) | no |
| xr_qi | android_vh_binder_restore_priority | 448/448 | CODEGEN | hand | order of 2 pure guards (cbz x19 / tbz w8) to the same exit; values loaded before | no |
| xr_qi | android_vh_binder_set_priority | 1012/1012 | BEHAVIOUR | hand | stock block @0x200 (non-qos caller, sync transaction) takes android_task_vendor_data(x19 = caller = t->from->task or current); ours used current: with t->from->task != current (binder_thread_read path) the request was recorded in the wrong task. The qos-caller branch uses current in both (stock 0x14c). | YES qi_hooks.c, rebuilt 04de8ce2; facts now equal |
| xr_qi | android_vh_fuse_request_fetch | 548/548 | CODEGEN | hand | IS_ERR_VALUE: cmn x,#0xfff b.hs == cmn x,#0x1000 b.hi (both: x >= -4095) | no |
| xr_qi | android_vh_futex_wait_queue_start | 572/564 | CODEGEN | hand | and x,#0xffff; cmp  vs cmp x, w, uxth (same 16-bit compare) | no |
| xr_qi | android_vh_percpu_rwsem_wq_add | 1848/1848 | CODEGEN | hand | ldr x,[x25,#0x48]! (writeback) vs add+ldr; same address | no |
| xr_qi | android_vh_record_rwsem_lock_starttime | 616/616 | CODEGEN | hand | two tail blocks swapped; 53/63 blocks exact, 0 CFG differences, facts equal | no |
| xr_qi | android_vh_rwsem_opt_rspin | 616/616 | CODEGEN | hand | stock threads the post-trace branch on flag w26 (tbnz -> 0x15c else 0x250), ours returns to the common 0x158 which tests the same flag (cbz w26): same targets | no |
| xr_qi | do_qos_inherit | 1052/1056 | CODEGEN | hand | argument w3 kept in a register (stock) vs spilled to [sp+8] (ours), shift/compare on it identical; facts equal except the callee naming | no |
| xr_qi | get_alloc_task_qi | 296/292 | CODEGEN | hand | two tail blocks swapped, facts equal, 0 CFG differences | no |
| xr_qi | init_module | 360/360 | CODEGEN | hand | first-error accumulation: stock cmp w19,0; ccmp w0,0,eq; csel lt; ours cmp w0,0; ccmp w19,0,lt; csel eq: both keep the first negative result; 0x7807 constants equal; registration order equal | no |
| xr_qi | iter_percpu_rwsem_qos_inherit | 988/988 | CODEGEN | hand | unlock tail blocks reordered (fall-through <-> b); after pairing 59/63 exact, 0 CFG differences | no |
| xr_qi | iter_rwsem_qos_inherit | 796/796 | CODEGEN | hand | cmp x0,x1 vs cmp x1,x0 with eq/ne only | no |
| xr_qi | qi_acquire_gpu_blockeds | 184/168 | CODEGEN | hand | loop: stock exits when i == max-1 before the increment and returns i+1; ours increments then compares to max: break returns max in both, exhaustion returns the count in both, empty list 0 in both | no |
| xr_qi | qi_set_async_binder | 760/756 | CODEGEN | hand | async flag read from the stack local at use (ours) vs kept in a register (stock): same local; the 7 error exits, messages and return codes (0,-14,-22,-12) equal; invalid pid path prints with the same w1 | no |
| xr_qi | qos_event_handler | 1336/1340 | CODEGEN | hand | inlined list_add validation: stock checks next==new and next->prev!=head, ours additionally new==head (impossible, new is a fresh allocation); same exit; facts equal | no |
| xr_qi | undo_qos_inherit | 688/684 | CODEGEN | hand | atomic andnot: mvn+and vs bic | no |
| xr_qs | qievent_callback | 1976/2036 | CODEGEN | hand | all exits return 0 except -22 (null) and -1 (PF_EXITING): ours sets w0=0 in each, stock at the common 0x570; (0/3) test: cmp 3/cbnz == cbz/cmp 3; clear_draw_rtg unrolled: slots +0xc/+0x14, valid +4, tgid +8, other at +0x18 checked in both; calls equal. Not proven: the full unrolled tail of the loop (offsets consistent) | no |
| xr_qs | qos_reset_callback | 1120/1112 | CODEGEN | hand | ubfiz+bfi vs and+2x bfi (same mask 0xff00c03f, same fields); arg&0xf computed once (stock w23) vs at the call (ours): same value | no |
| xr_qs | qos_set_callback | 1632/1628 | CODEGEN | hand | same patterns as reset (prio read after get_task_prio call in both) | no |
| xr_qs | restore_qos_sched | 1708/1748 | CODEGEN | hand | ands/ccmp#3 vs ubfx/cmp#3/ccmp#0: both give 0x78 for x in {0,3}; draw-index search: stock tests grp==12 as a constant (id[0] is only set to 12 at init) and ids 1..3 from memory; ours loads id[0..3]: equal while id[0]==12 (it is never written elsewhere); no-match exits equal | no |
| xr_qs | set_rtg_grp | 3524/3592 | UNPROVEN | hand | calls equal (exchange_draw_rtg x2, move_existed_draw_rtg x1), strings equal, offset sets equal except stock-only [x28/0x10 > 0x78 bounds trap], pointer-select form (csel on &g_draw_grp[i]) vs index form restructured over 3.5 kB; NOT proven equivalent, needs an Opus pass; reachable index 0..3 only | no |

## Findings (G40f)

BEHAVIOUR differences proven from the raw listings and fixed in OUR source (both were earlier called CODEGEN/noise):

1. xr_qi `android_vh_binder_set_priority` (qi_hooks.c): in the non-qos-caller, synchronous branch stock block @0x200 uses `android_task_vendor_data(x19)` where x19 = caller (`t->from->task`, else current); ours used `current`. Differs when the hook runs in the receiving thread (caller != current): the request (req, f30 pid, wait state) was recorded in the wrong task. Fix: `android_task_vendor_data(caller)`. The qos-caller branch correctly keeps `current` (stock 0x14c).
2. mi_low_lat_mem `mi_pre_compact_init` (llm_main.c): on `sysfs_create_group` failure stock prints only "add pre_compact sysfs fail %d" (branch into the shared printk) and then unwinds; ours fell through and printed an extra "search pre_compact symbols fail: %d". Fix: label `unreg_hooks` after that print, sysfs failure jumps to it.

Rebuilds (real Kleaf, one at a time, bazel-out removed): xr_qi md5 04de8ce2b2316ee9c8ebf94ae5b0f5e6 (was a3769edd25504bcfb68b3869493a9d1c; stock ea178f044ad316460b219a146c8f3f7f); mi_low_lat_mem md5 cdc48c230842f7c1da4f8b728f1ca16c (was d45cb60fc0aed34283b7679554b4381a; stock 88526bdafed09c69d00dd521b05ab719). compare_ko_v2: xr_qi function-set 369, function-bytes 19 of 369 (same set), kCFI 339 / binding / objects 258 PASS, anon-data + jump-table + altinstructions FAIL as before; mi_low_lat_mem function-set 113, function-bytes 28 of 113 (same set), kCFI 100, objects 85, anon-data PASS, jump-table + altinstructions (208 vs 203) FAIL as before; preamble_cmp 0 mismatches in both; symcmp5b: binder_set_priority and mi_pre_compact_init CLEAN-POL; facts of binder_set_priority equal. Source md5: qi_hooks.c acc0df94793a45e98110eebab9facbf1, llm_main.c 0cb4020e2a495740b5d4aeb4821f527f (both LF). mi_game and xr_qs were not edited (no proven difference), their .ko stay 3e8fed6a... and 1d7f5cbd....

Checked and found equal (not fixed because nothing to fix) worth knowing: scene signedness in cma/dma/slowpath/direct_reclaim handlers (stock `cmp #0x99f`/`cmp #0x9a0` are array-bounds traps; the reachable scene range is -1 (skip) and 0..4, identical in both); slowpath order check (stock ccmp chain = ours bit mask 0x211, set {0,4,9}); dfx_info_reset loop (swapped base roles, 3 iterations both); init_module of mi_low_lat_mem (stock checks cma_pool_init_shrinker, constant 0 in both); xr_qs restore_qos_sched draw-id 12 constant.

Limits of the method (be honest): blk.py pairs blocks by register-less signatures, so operand ORDER of a 2-register compare followed by lt/gt/hs/hi conditions and data-flow swaps between registers are not caught by the tool; hand-read functions were read for these. "tool" rows are NOT proof.

## Remaining, ordered by risk
1. mi_game UNPROVEN: `migt_ioctl` (2.3 kB: other commands, user-copy bounds), `migt_hardwork` (3.6 kB, list_add placement, locking). xr_qs UNPROVEN: `set_rtg_grp` (3.5 kB restructured pointer/index selection).
2. mi_game UNREAD (facts shown in the table): `game_lb_control`, `game_load_reset`, `mist_frame_load_history_update`, `flt_cal_freq`, `game_setup_wrender_list`, `freedom_window_app_set`, `minor_window_app_set`.
3. "tool" rows (36 mi_game, 14 mi_low_lat_mem): re-read the raw listing of the biggest ones: `pre_do_compact`, `dma_heap_buffer_alloc_end_handler`, `update_cpu_frame_load`, `uclamp_frame_load`, `update_frame_load`, `update_pkg_load`, `xr_user_roll_wk`.
4. Independent Opus review of the two new fixes and of set_rtg_grp before any load decision (all four modules remain PARTIAL, DO NOT LOAD, no hardware test).
