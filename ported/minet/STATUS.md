# minet - COMPILES-only (PARTIAL): 18 of 44 stock functions byte-identical to the HyperOS 4.0 (OS4) stock

Reference binary: `<work-dir>` (vermagic 6.6.57-android15-8; own BTF base, `START_ID=141631` for btf_dump).
Template: XRING tree `work/re/xiaomi-modules/mi_net/minet/` (minet_core.c, minet_nl.c, minet_nl.h, minet_debug.h), used unchanged (copied here by the main session; BUILD.bazel + ThinLTO defconfig exist).

Result (real Kleaf build on 6.6.144, ThinLTO defconfig, compare_ko.py):
- (a) COMPILES.
- (b) function set DIFFERS: stock 44 functions, rebuilt 23. Of the 23 built functions 18 are byte-identical; relocations identical; exports (0) OK; modinfo OK.
- Different bytes: `hook4`, `hook6`, `minet_nl_cmd_set`, `init_module`, `cleanup_module` (they reference the missing new code: tc limiter hooks, idletimer, extra netlink commands).
- 27 of the 60 stock imports are missing in the rebuilt module because the new code is missing (mod_timer, timer_delete, init_timer_key, queue_work_on, cancel_work_sync, register/unregister_pm_notifier, set_normalized_timespec64, ns_to_timespec64, ktime_get_with_offset, _raw_read/write_lock_bh, mutex_lock/unlock, ...).

What is missing (21 functions; no template in any tree we have):
- TC limiter: `minet_tc_limiter_init/finit`, `tl_after_filter_in`, `add_tc_app`, `clear_tc_app.llvm.<hash>`, `minet_set_tc_enable`, `minet_set_tc_list`, `minet_dump_tc_list`, `minet_nl_tc_enable_set/_list_set/_list_dump`, `minet_nl_reply`, global `m_tc_enable`. New Xiaomi code, no public template.
- In-module copy of the netfilter IDLETIMER target: `mi_idletimer_tg_checkentry_v1/_target_v1/_destroy_v1`, `idletimer_tg_expired/_work`, `idletimer_resume`, `reset_timer.llvm.<hash>`, `print_idletimer_info_safe`, `minet_idletimer_notify`, object `m_idletimer_info.llvm.<hash>` (80 bytes). Derived from kernel `net/netfilter/xt_IDLETIMER.c` (a MiCode copy is in `work/srcsurvey/micode_*`).
Next step if someone continues: reconstruct these 21 functions from `<work-dir>` (stock disassembly), then re-diff hook4/hook6/nl_cmd_set/init/cleanup.
Build: `bash work/re/src/a17_push.sh minet` then `<work-dir> minet` on the server.
