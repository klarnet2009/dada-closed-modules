# mca_wireless_revchg - COMPILES-only / PARTIAL (18 of 45 functions byte+relocation identical; reference = OS4 stock)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_strategy/strategy_wireless/mca_wireless_revchg.c` + `inc/mca_wireless_revchg.h` (flattened). The OS4 stock is NOT the template revision: it is newer in places (two new workers) and older in others
(no `tx_ss`/`tx_tdie`, other process_event layout), so the template needs function-by-function adaptation.
Done: (1) struct `mca_wireless_revchg` +2 `delayed_work` `user_enable_revchg_work`/`user_disable_revchg_work` right after `pen_data_handle_work` (stock offsets 0x798/0x820, `rev_boost_src` moves to 0x8a8; this fixed every
small function that was off by 0x110) + the two trivial workers (`mca_wireless_rev_enable_reverse_charge(true/false)`) + INIT_DELAYED_WORK in probe; (2) sysfs `reverse_chg_mode` store queues them
(`schedule_delayed_work(val ? &user_enable : &user_disable, 0)`, no rc check) as the stock does; (3) process_event: the stock switch (jump table, 0x53 entries) handles USB 0/1 (with `support_tx_only`), wireless 2/3, 8 (int change),
0x20 cboot, 0x26 antiburn, 0x27 btb, 0x29 lpd, 0x2b cc-short, 0x32/0x33 abnormal/restore, 0x51/0x52 (pen hall / ppe hall) - header values set accordingly (INT_CHANGE=8, PEN_HALL=0x51, PEN_PPE_HALL=0x52 are inferred from the table order);
(4) new public header `stubs_a17B/include/mca/strategy/mca_wireless_revchg.h` (13 + 2 `pen_charge_state_notifier_*` prototypes): all 15 export CRCs match the OS4 stock; (5) `platform_class_wireless_get_tx_err_code` takes 3 args in the stock.
Unverified guesses (values not yet read from the stock): FIRMWARE_* (0..3), enum rev_boost_src (PMIC_REV_BOOST..BOOST_SRC_MAX), EN_SRC REV_EN_BOOST, REVCHG_TYPE, VUSB_PRESENT_STAT, CHARGE_DFX_WLS_TRX_* (0x31..0x34), STRATEGY_STATUS_TYPE_REV_TEST.
Temporary bridge in the .c: `get_tx_ss`/`get_tx_tdie` macros (not exported by the OS4 wireless provider; the stock code does not call them) - those code paths must be removed once the real stock logic is transcribed.
Result vs OS4 stock: compiles (Bazel build ok, 45 functions, same set); 18 functions identical, 27 differ; exports 15/15 + CRCs OK; imports differ only because the template calls things the stock does not (and vice versa).
Biggest remaining diffs (masked word diff): process_event 780, mca_wireless_rev_charge_config 724, enable_reverse_charge 252, update_fw_mainthread_work 225, update_fw_version 187, pen_data_handle_work 165, charge_config_work 139, set_reverse_src 146, sysfs_show 96.
Next steps: read the stock disassembly of those functions (`<work-dir> mca_wireless_revchg -c 3 <func>` after `work/a17/portB/qpush.sh mca_wireless_revchg`) and rewrite them; pin `__LINE__` with `#line`.
Tooling: `work/a17/portB/` (push.sh = Bazel build+compare_ko, qpush.sh = 1-s LTO-emulation compile + per-function diff, undecl.sh, lines.sh, fd.sh, fn.sh, cnt.py), providers = generated OS4 stubs `//re/stubs_a17B/os4` (gen_stubs.py).
