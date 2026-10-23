# xr_qi / xr_qs on the GLOBAL OS4.0.0.7 base - status (goal-G20c, 20.10.2026)

COMMUNITY-ONLY scheduler modules: never flash, never in a ROM build, DO NOT LOAD until an independent review. mi_reclaim (shares hooks with xr_qi) was not touched. Nothing was loaded, flashed or committed.

Method: real Kleaf build (`--lto=none --config=stamp`) of `work/re/src/xr_qi` and `xr_qs` against the global stock `<work-dir>`, `compare_ko_v2.py` + `preamble_cmp.py`, register-masked multiset diff (`opb.py`), normalised per-function diff (`g20c_tools/fd2.sh`). Tools: `work/re/src/g20c_tools/`; scratch `<work-dir>` (bazel-out deleted after each compare).

## Numbers (before = start of G20c = G14a result; after = final)
| | function set | function-bytes differ | relocs differ | kCFI | objects | exports | imports | anon-data | note |
|---|---|---|---|---|---|---|---|---|---|
| xr_qi before | 369 PASS | 19 of 369 | 13 | 339 PASS | 258 PASS | 14 PASS | 113 PASS | FAIL (iter_qos_inherit table) | |
| xr_qi after | 369 PASS | 19 of 369 | 13 | 339 PASS | 258 PASS | 14 PASS | 113 PASS | FAIL (same) | all 19 CODEGEN |
| xr_qs before | 118 PASS | 5 of 118 | 5 | 110 PASS | 114 PASS | 0 PASS | 56 PASS | FAIL (set_rtg_grp table) | .altinstructions 252 vs 250 |
| xr_qs after | 118 PASS | 5 of 118 | 5 | 110 PASS | 114 PASS | 0 PASS | 56 PASS | FAIL (same) | .altinstructions 252 = 252; qievent_callback multiset 75 -> 38 |

Byte equality is secondary: 350/369 and 113/118 functions are byte-identical; the behaviour question is what the differing 24 functions do.

## Verdict per differing function (CODEGEN = same behaviour, other instruction selection/layout)
xr_qi (19): `__qi_set_task_qos` C (qos 2/1/3 select evaluated both ways: equal), `qi_set_async_binder` C (bool kept in another register/earlier; frame 0x70 vs 0x60), `android_vh_binder_set_priority` C (frame, test order of independent conditions, cold block moved), `android_vh_binder_restore_priority` C, `android_vh_binder_proc_transaction` C (multiset 0), `android_vh_fuse_request_fetch` C (`x >= -4095` as `cmn #0xfff;hs` vs `cmn #0x1000;hi`), `android_vh_futex_wait_queue_start` C (`and #0xffff;cmp` vs `cmp uxth`), `iter_percpu_rwsem_qos_inherit` C, `android_vh_percpu_rwsem_wq_add` C, `iter_rwsem_qos_inherit` C, `android_vh_alter_rwsem_list_add` C (one source-form fix: skip-trace bool is `type != WRITE`, multiset 16 -> 12), `android_vh_rwsem_opt_rspin` C, `android_vh_record_rwsem_lock_starttime` C (multiset 0), `get_alloc_task_qi` C, `do_qos_inherit` C (two stack slots), `undo_qos_inherit` C (`bic` vs `mvn+and`), `qi_acquire_gpu_blockeds` C (`ret++ == max-1` vs `ret+1 == max`, equal modulo 2^32), `qos_event_handler` C (inlined `__list_add_valid` condition order, block order), `init_module` C (ccmp operand order of the first-error-wins chain; same registration order and error path).
Byte-identical but anon-data FAIL: `iter_qos_inherit` jump table: same index->callee map (0 binder, 1 mutex, 2 rwsem, 3 percpu rwsem, 4 futex, 5 fuse; every entry checked against the callee at its target), only the block order differs. A `switch` in either order gives the same layout; reverted.
xr_qs (5): `qievent_callback` C (what remains: 4x unrolled clear_draw_rtg tails, branch polarity), `qos_set_callback` C (one hoisted `task->prio` load), `qos_reset_callback` C (`bfi` vs `ubfiz+orr`), `restore_qos_sched` C (policy_backup test `ands/ccmp` vs `ubfx/cmp/ccmp`: both == 0 or == 3; group id compared with the immediate 12 vs loaded `g_draw_grp[0].id` = 12 set in init), `set_rtg_grp` C (stock has a trapping array-bounds check `brk #1` for idx > 3, ours proves the range: idx comes from get_draw_idx() in 0..3).

## Behaviour audit
No proven BEHAVIOUR difference in either module, so no behaviour fix in our source was needed. Covered by byte identity or by PASS checks: hook registration order/error paths (qi_vh_*_register, init/exit; init_module only ccmp order), tracepoint and rwsem hooks (function set 369; the read-trylock hook and `qi_rwsem_reader_retry_lock` are absent in the global stock and in ours), sysfs/params/permissions (objects 258/114 with content+relocs, modinfo), exports/imports with CRC (xr_qs imports include plat_game_vip_check), qs masks/constants (0xf/0xe/0xc0, BIT(4)/BIT(5), vip prio 5, SCHED_RR|RESET_ON_FORK, 120/-1), NULL/error paths (identical compare/branch sequences), struct offsets (no offset differs in any diff, so pahole/BTF was not needed).
Not verified: dynamic behaviour (nothing was run), stock paths exercised only on hardware, the exact meaning of the stock `brk #1` bounds trap source form.

## Source edits in G20c (all codegen-only)
- xr_qi: `qi_rwsem.c` trace_qi_rwsem_skip argument `!= RWSEM_WAITING_FOR_WRITE`; `qi.c` init loop `!ret & (err < 0)` (no output change). A `switch` form in iter_qos_inherit tried and reverted.
- xr_qs: put_task_struct open-coded with call-site `always_inline` on `__refcount_sub_and_test` in correct_qos_sched / qos_inherit_callback; `asm("" : "+r"(action))` + call-site inline of clear_draw_rtg in qievent_callback (switch is not specialised, cmp #6 like the stock); `likely()` variant rejected (outlines clear_draw_rtg).

## Hacks and guesses (see STATUS.md of each module)
Forged CRC pads (GKPAD) on xr_qi exports, forged buildmeta/depends, stub providers, call-site always_inline hints, struct field names ours, `asm` barrier in qievent_callback.

## Next targets
1. xr_qs clear_draw_rtg unrolled-loop shape (index-in-register shared tails) and the `RTG_DRAW_ID` constant folding in restore_qos_sched/set_rtg_grp.
2. xr_qi iter_qos_inherit block order (only remaining anon-data FAIL) and the frame/bool shape of qi_set_async_binder and binder_set_priority.
3. Independent Opus review of xr_qs set_rtg_grp / draw-group logic and the xr_qi rwsem hooks (alter_rwsem_list_add, do_/undo_rwsem_qos_inherit) before any load decision.
