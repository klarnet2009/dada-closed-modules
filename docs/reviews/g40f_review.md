# Independent review of goal-G40f (xr_qi / mi_low_lat_mem / mi_game / xr_qs), 20.10.2026

Reviewer: read-only Opus pass. Nothing built, loaded or run on a phone; no git; sources unchanged.
Stock: GLOBAL OS4.0.0.7 `<work-dir>` (md5 xr_qi ea178f04, mi_low_lat_mem 88526bda, mi_game fe4ba456, xr_qs bf597ada).
Ours: xr_qi `g40f/built` 04de8ce2, mi_low_lat_mem `g40f/built` cdc48c23, mi_game `g27a/built` 3e8fed6a, xr_qs `g23a/built` 1d7f5cbd.
Method: own raw `llvm-objdump -dr` per function (symbol section cut by value/size, relocation targets and strings resolved, branch targets as function offsets:
`<work-dir>`), every block of stock and ours read by hand and path-paired; jump tables decoded from the section bytes (`jt.py`);
trace_printk format slots (`tpf.py`) and long format strings (`str.py`) compared byte for byte; tables with `g33a/tbl_audit3.py` on the builds above.
No lifter, no facts/atoms used as proof. Scratch `<work-dir>` (3.6 MB: tools, `L/` listings, `tbl/` audit outputs).

Offsets below are offsets from the function symbol (stock `S+x`, ours `O+x`).

## Summary

| module | function | verdict | severity | how |
|---|---|---|---|---|
| xr_qi | android_vh_binder_set_priority: G40f fix (non-qos caller, sync) | MATCHES | - | VERIFIED: S+1fc..274 vs O+14c..1c4, vendor_data(x19=caller) in both |
| xr_qi | android_vh_binder_set_priority: qos caller, qos target | **DIFFERS** | MEDIUM | VERIFIED: S+148 falls through into the inherit block S+14c; O+148 `b 270` skips it |
| mi_low_lat_mem | mi_pre_compact_init: G40f fix (sysfs failure) | MATCHES | - | VERIFIED: S+2f0->2a0 vs O+2f4->2b8 |
| mi_low_lat_mem | mi_pre_compact_init: pool(9) kmalloc failure | **DIFFERS** | LOW | VERIFIED: S+240 `b c8` (clears mci+0x30) vs O+254 `b cc` (dangling mci+0x30) |
| mi_low_lat_mem | init_module, every error exit (16) | MATCHES | - | VERIFIED, both listings |
| mi_game | migt_ioctl (11 commands) | MATCHES | - | VERIFIED: every block + jump table |
| mi_game | migt_hardwork | DIFFERS (log only) | LOW | VERIFIED: debug printk args 3/4 swapped (S+dd0 vs O+d84) |
| xr_qs | set_rtg_grp | MATCHES | - | VERIFIED: every path paired, jump table, helpers byte-equal |
| mi_game | mist_frame_load_history_update (inlined update_avg_freq) | **DIFFERS** | MEDIUM-LOW | VERIFIED: S+35c overwrites w27 (cnt) that S+228 reuses on the next cluster |
| mi_game | flt_cal_freq | DIFFERS (trace only) | LOW | VERIFIED: tracepoint args 2/3 swapped (S+99c vs O+91c) |
| mi_low_lat_mem | pre_do_compact | DIFFERS (dfx histogram only) | LOW | VERIFIED: success exit S+4e4..500 sets pct4=pct9=100, ours computes |
| mi_game | game_lb_control, game_load_reset, game_setup_wrender_list, freedom_window_app_set, minor_window_app_set | MATCHES | - | VERIFIED |
| mi_game | update_cpu_frame_load, uclamp_frame_load, update_frame_load | MATCHES | - | VERIFIED |
| mi_low_lat_mem | dma_heap_buffer_alloc_end_handler | MATCHES | - | VERIFIED (incl. all printk/snprintf argument slots and format strings) |
| all four | data tables (tbl_audit3) | MATCHES | - | VERIFIED: 0 unexplained; only known renames / dead "cma pool register failed" |

## 1. The two G40f fixes

### 1a. xr_qi android_vh_binder_set_priority

Call sites (kernel `drivers/android/binder.c`, `binder_transaction_priority()` line 865, called from `binder_proc_transaction()` line 3083 under
`binder_inner_proc_lock` = sender context, and from `binder_thread_read()` line 5217 = receiving thread context). caller = `t->from ? t->from->task : current`:
sync + sender context: caller == current (sender); sync + receiver context: caller = sender != current; oneway (`t->from == NULL`): caller = current
(sender in proc_transaction, the receiver itself = `task` in thread_read). Kernel threads: binder transactions come from user threads; init_task maps to
`vendor_data_pad` identically in both. No RCU/lock around the `t->from` read in either build (same race as stock).

Paths read in both listings (guards S+4c..6c, caller S+70..80, refcount get S+2b0, get_task_qi NULL -> put S+294, is_qos_task):
- non-qos caller, oneway: nothing, put caller. MATCHES.
- non-qos caller, sync (the G40f fix): S+200..274 / O+14c..1c4: `v = vendor_data(caller)`; if `v->f1_b4`: lock v+0x34, v+0x10 = t, v+0x30 = task->pid,
  v[1] &= 0xf0, unlock; `tv = vendor_data(task)`, tv+4 = caller->pid. Data written identical. **Fix MATCHES (VERIFIED).**
- qos caller, oneway without `qi->async_binder_todo` (qi+0xa9): put caller only. MATCHES.
- qos caller, non-qos target: iter state in `vendor_data(current)` (v+0x18 = t, v+0x20/0x28 = 0, depth bits 5..6 = 1), do_binder_qos_inherit, iter_qos_inherit
  on 0, iter state cleared, async_binder_todo cleared for oneway. MATCHES.
- **qos caller, qos target: DIFFERS.** Stock S+f8 `tbz w0,#0,14c`; the notify block S+fc..148 (lock `tqi` at offset 0, ev.to/qos/prio, QIEVENT 6, unlock)
  has no branch at its end and FALLS THROUGH into S+14c, i.e. stock notifies AND then runs the whole inherit block (do_binder_qos_inherit + iter_qos_inherit,
  clears async_binder_todo). Ours O+148 `b 270` goes straight to put_task_struct. Consequences in ours: a qos target never gets the binder inherit
  (t+0xb8 stays -1), and `async_binder_todo` is never cleared on this path (also when caller == task, the oneway receiver case), so later oneway
  transactions of that thread keep inheriting where stock would skip. Scheduling/QoS behaviour only, no memory-safety effect.
  Fix (qi_hooks.c, android_vh_binder_set_priority): drop the `else`: keep the `if (is_qos_task(task)) { ... notify ... }` block and run the
  `v = android_task_vendor_data(current); ...; do_binder_qos_inherit(...)...; async_binder_todo` block unconditionally after it; also fix the comment
  "a qos target only gets QIEVENT_QOS_CORRECT".
- Helpers: is_qi_feat_enabled, get_task_qi, is_qos_task, get_task_qos, get_task_prio, do_binder_qos_inherit, put_task_struct: normalised listings identical;
  iter_qos_inherit differs only in jump-table block order, decoded case->callee map identical (binder/mutex/rwsem/percpu_rwsem/futex/default). VERIFIED.

### 1b. mi_low_lat_mem mi_pre_compact_init and init_module

mi_pre_compact_init, every exit (stock S / ours O):
- "already initialized" cleanup of both pools (drain, kfree, NULL): MATCHES.
- pool(4) kmalloc NULL: mci+0x30 = NULL, "pools init fail -12", return -12: MATCHES.
- **pool(9) kmalloc NULL: DIFFERS.** Stock drains pool(4), kfree, then S+240 `b c8` -> `str xzr,[mci+0x30]` -> print -> -12. Ours O+254 `b cc` skips
  O+c8, so mci+0x30 keeps the freed pointer (source llm_main.c:1207-1211 has no `MCI_PTR(0x30) = NULL` after kfree). Today harmless: init_module then
  fails (err_groups, no pools_exit, module memory freed) [VERIFIED by both init listings]; latent double-free/UAF if the error path or a re-init ever
  touches the pools. Fix: `MCI_PTR(0x30) = NULL;` after `kfree(pool);` in that branch. LOW.
- vh compact_bypass register failure: print, continue (both). alloc_pages_reclaim_bypass failure: print, unregister compact_bypass, print "hooks fail",
  pools_exit, return ret: MATCHES.
- register_kprobe failure / lookup("try_to_free_pages") == 0: "search pre_compact symbols fail: -1", unregister both, pools_exit, -1: MATCHES.
- sysfs_create_group failure (the G40f fix): stock S+2f0 sets x0 = "add pre_compact sysfs fail", w1 = ret and branches into the shared printk S+2a0,
  then unregister both + pools_exit, return ret; ours O+2f4 -> O+2b8 the same. **Fix MATCHES (VERIFIED).** Success: print, return 0. MATCHES.
init_module (both listings): mi_smart_refill_init (-12, nothing), dmabuf_data_tmp/sys_mem_data init (kfree tmp, cma_area_cleanup, refill_exit),
cma_pool_init_shrinker (stock checks a constant 0, ours drops it: dead), cgroup cftypes, kobject, sysfs_create_groups, mi_pre_compact_init (no
mi_pre_compact_exit in stock either), mi_dfx_init, mi_memcg_vendor_alloc, kthread_create (ERR_PTR stored, PTR_ERR returned), rvh/vh registrations
with their unwind chains: same order and the same releases at every exit. MATCHES (VERIFIED). Stock quirks kept by both: the rvh
try_alloc_pages_gfp stays registered on later failures; mi_memcg_free gets NULL as data (ours leaves x0 undefined, callee never reads it).

## 2. Previously unproven / unread functions

- **migt_ioctl: MATCHES.** Jump table (cmd 1..11, stock .rodata+0, ours .rodata.migt_ioctl): 1 boost, 3 update_render_info(1), 4 ceiling list,
  7 flt_adjust_dequeue, 8 flt_update_gpu(arg), 9 sf, 10 target delta, 11 fence, 2/5/6 and others return 0. User copies: cmd 9 only if
  (cmd & 0x3fff0000) == 0x200000, copy 32 bytes, uid at +0x1c, vsync at +8; cmd 10 copies 4 bytes, v < 1 -> 0 else 100000/v (unsigned). No permission
  checks in stock either; always returns 0. Boost path (uid change, ceiling work, thresholds, render_pid, sched_clock thresh, hrtimer/delayed work,
  policy switch, history update, item index % 10) and the ceiling list (dup scan, shift + [9] for n >= 10) block by block identical. VERIFIED.
- **migt_hardwork: DIFFERS only in a debug log.** Locking (read_lock_irq/unlock of xu+0x10 on all paths), seqcount snapshots, refcount
  inc_not_zero/get/dec, BUG_ON, delay list add/del_init with validity checks, gc tagging, user_array bounds, sort, stask pick, ranking, puts, resets:
  identical. Difference: `migt-gtc: max %d mindex %d, max_r %llu, m_exec %llu` - stock passes (max_i, mindex, max of fps_exec_est(+0x8),
  max of fps_mexec(+0x18)) (S+dd0: x3 kept from the +0x8 max, x4 = x24 the +0x18 max); ours passes the two values the other way (O+d84 x3 = x23).
  Selection `sel` is identical. Fix migt_f.h:457-458: `max_i, mindex, m_exec, max_r, 50`. LOW (only with migt_debug_l). Codegen only: sched_ravg_window
  read before vs after set_vip_prio; ours also stores `*work = 0` on the `!*work` path (stock only clears the flag): same lost-wakeup race either way.
- **xr_qs set_rtg_grp: MATCHES.** Paired: early skip (2..10), from-group path with get_draw_idx + inlined clear_draw_rtg (4 slots x ui/render, break on
  first, other==NULL clears tgid/valid), lvl switch (jump tables decoded: 6 loop, 7->14, 8->13, 9->12, 10->11), case-6 search (p==NULL variant too),
  qos&0xc0 -> 13, skip, !set (get_static_qos_lvl unsigned > 5 -> -1), non-ui/non-render, idx(new) with orig==12 (move or exchange) and orig!=12
  (stock bounds trap `x|0x10/0x18 > 0x78` unreachable for idx 1..3), new!=12 clearing of g[0]/g[idx(orig)], new==12 with idx(orig) (move/exchange),
  [0] valid+tgid, free-slot search + move of ui/render with get/put + trace + valid=moved, final set 12; unlock; rtg_set_flag = bit 15 of qs word 0.
  exchange_draw_rtg / move_existed_draw_rtg byte-equal. VERIFIED.
- **mist_frame_load_history_update: DIFFERS (MEDIUM-LOW).** In the inlined update_avg_freq stock keeps the count in w27: `madd w27,w27,w10,w19`
  at the loop head S+228 (sum_history_margin = w27*flt_target_delta - total/10000) and `cinc w27,w12,ne` at S+35c (count = !!sum_freq_rt[j][0] +
  !!sum_freq_rt[j][1], used for delta_ms); the back edge S+3b8 -> S+224 re-executes the madd, so for cluster j >= 1 stock uses the count of cluster j-1,
  ours always the last_frame_period count (O+274 `madd w28,w9,w4,w24`, n kept separately in w12). This changes d2 and therefore `real_target_delta`
  (-> next frame need_target_delta) when cal_cur_cid == 1 and flt_choose_avg_num[1] != 1, and the debug prints; equal in the steady state (both counts 2).
  Fix flt.c update_avg_freq: reuse `cnt` (`cnt = !!s0 + !!s1; delta_ms = cnt * flt_target_delta;`), drop `n`. Everything else (gpu calibration, cid,
  history roll, unrolled clears, ftn toggle, preboost/irq_work) MATCHES; codegen: cal_cur_cpu from current->cpu vs cpu_number (same value), frame_time_num
  and idx trap forms, cluster_number signed vs unsigned loop bound (equal for >= 0).
- **flt_cal_freq: DIFFERS (trace only, LOW).** trace_flt_cal_freq_mark: stock x2 = cap_states max load, x3 = uclamp_frame_load (S+92c/99c), ours the
  reverse (O+91c `ldp x2,x3,[x29,#-0x10]`). TP fields are (frame_load, frame_load_max), so the two trace fields are swapped vs stock. Fix flt.c:896:
  `trace_flt_cal_freq_mark(policy->cpu, max_load, frame_load, ...)`. Everything else MATCHES: guards, real_util/real_freq, in-frame boost, fence, the
  enable gate, prev-item fallback, uclamp/disable_soc_util branch, flt_load/t/margin, avg choice and force check, render scaling, adaptive clamp, store
  of cap_states.freq (8 bytes), reasons, ddr, preboost math, trace_printk slots (format strings equal). Codegen: target kept 64-bit in ours where stock
  truncates next_freq to 32 bit (frequencies in kHz fit).
- **pre_do_compact: DIFFERS (LOW, dfx statistics only).** On the success exit (mci.raw[1] set) stock jumps S+4e4..500 to S+5fc with pct4 = pct9 = 100;
  ours O+4d4 -> O+584 computes pct from the pool counts (source llm_main.c:1712-1716 for all exits). Only dfx_fill_bucket (dfx+0x8f4..0x918) can differ
  (when a pool dropped below 81 % of the target after "enough"). Fix: compute pct4/pct9 only in the retry-exhausted branch, keep 100/100 on success.
  Rest MATCHES (zone sums, ttfp block + kCFI call, buckets, stats, timeout printk/snprintf/fault fill, loop record of 72-byte history, retry prints,
  final report; long format strings byte-equal).
- **game_lb_control: MATCHES** (one read of force_forbidden_walt_lb in stock for both vip and ip choices vs two in ours: race only).
  **game_load_reset: MATCHES** (bounds trap forms only). **game_setup_wrender_list: MATCHES** (neither build passes a 4th argument; trace_printk slot
  +0x40 vs +0x30 = same format). **freedom_window_app_set: MATCHES; minor_window_app_set: MATCHES** (stock and ours minor-vs-freedom deltas identical:
  unbind(1), no cpumask check, is_minor = 1).
- **update_cpu_frame_load, uclamp_frame_load, update_frame_load: MATCHES** (all paths incl. the irq path BUG, the 4/5/6/7/10 case numbers, the reason
  1..4 cascade, mulpct/muldiv, the inlined adjust_slowpath search; trap thresholds 21 vs 22 for cur_idx are trap forms).
- **dma_heap_buffer_alloc_end_handler: MATCHES** (scene guard, proc type, buckets, counters/max under the dfx lock, timeout, snapshot of rec+0x90/0x94/
  0xa0/0xa4, pool walk type 0/1, every printk and snprintf argument slot, fault fill).

## 3. Tables (tbl_audit3, builds above)

xr_qi, xr_qs, mi_low_lat_mem: 0 differing named objects; jump tables JT-EQUIV (iter_qos_inherit, qievent_callback, set_rtg_grp); mi_low_lat_mem only the
dead "cma pool register failed" string. mi_game: renames with equal content (migt_debug_l = the stock local migt_debug at .bss+0x8: migt_table+0x48
points to it in both, VERIFIED from the stock RELA), tperiod_map/flt_tperiod_map = stock anonymous tables (MOVED, equal), migt_ioctl JT-EQUIV;
0 unexplained. trace_printk format set of mi_game: 26 = 26, equal.

## 4. Not covered / limits

cma_alloc_end_handler and the other G40f "tool" rows were not re-read (outside this request). Kernel-side layout of binder_transaction/binder_thread
offsets (0x28, 0x58, 0x60, 0x6c, 0xa8, 0xb8, 0x1b8) on our 6.6.144 not re-checked here (G40a gap: hook arguments). Nothing tested on hardware; all four
modules stay PARTIAL / DO NOT LOAD. Four source fixes proposed above (binder qos-target fall-through, pool-9 NULL, avg_freq count reuse, pre_do_compact
pct on success) plus two log/trace argument swaps; none applied.
