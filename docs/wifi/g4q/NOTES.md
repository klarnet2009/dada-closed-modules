# g4q notes (18.10.2026): qca_cld3_* (5 Wi-Fi drivers) - kernel-header vintage of the stock (6.6.57), 17 recovered Xiaomi/old-CAF behaviours, removal of newer-CAF code

Server scratch `<work-dir>` (layout of g4p: `wlan/pkg/qcacld3` working package = g4p package + specs 32-55 + `xiaomi_ext/xm_compat_6657.h`, `wlan/snap` frozen no-hack copy (Kleaf label `//re/qcawlan_g4qb/...`), `wlan/hack` = pkg + linehack (label `qcawlan_g4qh`), `built/{b8,b9,b10,b11,b11x,h12}_<chip>.ko`, `logs/`, `dump/`, `scripts/`: g4p scripts with paths renamed + `seq.sh` (ordered call/string list stock vs real build), `cc.sh` (call multiset), `fdc.sh` (compact stock disassembly), `ff.sh` (find a function in the source), `final_r.sh` = whole chain b11 peach_v2 -> classification -> hack JSON -> b11x 4 chips -> h12 5 chips, ~40 min; one real Kleaf build of one chip takes only ~4 min). Symlinks `<build-dir>` created. Local copies: `work/a17/g4q/` (specs/, xiaomi_ext/xm_compat_6657.h, replay.sh, tools/{ed.py,up.sh,final_r.sh}, logs/, dump/lh_b11.json).
Replay: g4p package (g4p/NOTES.md) + `replay.sh <pkg>/qcacld3` (specs 32-55 with the g4o ed.py + the header). The line hack JSON `dump/lh_b11.json` is valid only for exactly this source state.
Lab fix: the `cflags_*` / `flags_*.txt` copies of g4p still pointed at the headers of the OLD package `qcawlan_g4o` (so header edits were invisible in the lab); renamed to qcawlan_g4q. mkflags.py now also passes `-include xiaomi_ext/xm_compat_6657.h`.

## Result (real Kleaf builds, fcmp.py; byte-identical functions of N stock functions, WITHOUT / WITH #line hack; g4p in brackets)
| variant | N | no hack | with hack | only-stock | only-ours |
| peach_v2 | 18446 | 17791 [17772] | 18275 [18255] | 0 | 0 [1] |
| peach | 18324 | 17391 [17371] | 17866 [17846] | 0 | 0 [1] |
| kiwi_v2 | 18271 | 17622 [17604] | 18099 [18080] | 0 | 0 [1] |
| qca6750 | 15880 | 15274 [15257] | 15722 [15705] | 0 | 2 [3]: hdd_disable_wifi (452 B), hdd_get_wlan_driver_status (12 B) |
| wcn7750 | 17981 | 17333 [17315] | 17811 [17792] | 0 | 2 [3] |
peach_v2 without hack (offpairs2): STRUCT (real code) 41 [65; g4o 69], IMM 603, OTHER 8, REG 2, MISSING 1.
compare_ko_v2 (peach_v2, hack build, `logs/compare_ko_v2_peach_v2_hack.txt`): NOT STRICT-EQUIVALENT. PASS: function-set [was FAIL: only-ours netif_tx_disable], kcfi-preamble 17008/17008, imports 584 + CRC [was FAIL, 4 extra __tracepoint_rwmmio_*], exports, modinfo, buildmeta, depends, vermagic, notes, unowned-code. FAIL: function-bytes 171 of 18601 [210], relocs 194 [237], binding 1 (wlan_mlme_miracast_opt_timer_handler local in stock), objects (only-stock nss_mode_type; p2phc_hooks and opcls_*_cfis_lst contents), anon-data, bug-table/lines (161 of 195), jump-table (48 vs 48 entries now, contents differ), other-tables (.altinstructions 5390 vs 5391). preamble_cmp.py was not run (needs verify_pkg on the path).

## 1. Kernel header vintage of the stock (specs 33; shared by all 5 variants)
The OS4 stock was built against the headers of 6.6.57-android15-8, our tree is 6.6.144. Two inline groups changed in between and show in the object code:
- `netif_tx_stop_queue()` (6.6.144: WRITE_ONCE(trans_start, jiffies) + smp_mb__before_atomic + set_bit; 6.6.57: set_bit only) and its users netif_stop_queue, netif_stop_subqueue, netif_tx_disable. Effect in ours: hdd_stop_queues_for_ac / wlan_hdd_stop_non_priority_queue 280/288 B instead of 228, epping_stop_adapter 160 instead of 380 (the stock INLINES netif_tx_disable, ours kept the 268 B out-of-line copy: the "only ours" function of g4o/g4p).
- `readl()/writel()`: 6.6.57 calls log_read_mmio/log_post_read_mmio/log_write_mmio/log_post_write_mmio unconditionally, 6.6.144 first tests the static key of the rwmmio tracepoints (4 extra `__tracepoint_rwmmio_*` imports, 42 jump-table entries, readl 156 B instead of 128).
Fix: `xiaomi_ext/xm_compat_6657.h`, force-included (`-include`, added to g4_modules.bzl copts) before everything else: it renames the 6.6.144 definitions while linux/netdevice.h and asm/io.h are read (macro rename + forward declaration), then defines the 6.6.57 bodies under the original names (`#pragma clang diagnostic ignored "-Wmacro-redefined"` so that TUs which define their own pr_fmt still compile). This is a build-environment emulation, not restored Xiaomi code. Behaviour on the 6.6.144 kernel: no trans_start update when a queue is stopped (tx watchdog of a stopped queue), the log_* helpers test the tracepoints themselves.
Results: hdd_stop_queues_for_ac, wlan_hdd_stop_non_priority_queue, epping_stop_adapter, readl, writel, netif_tx_start/wake_all_queues byte-identical; only-ours netif_tx_disable gone in all variants; compare_ko_v2 imports PASS and function-set PASS (peach_v2).

## 2. Recovered behaviour (lab = ThinLTO emulation first, then real builds; each item replayable via specs/)
32 lim_fill_pe_session / lim_check_oui_and_update_session: Xiaomi block "Disabling HT40 for vdev %d for IoT AP": ACTION_OUI_LIMIT_BW (id 23) on 2.4 GHz with dot11mode 11N/11N_ONLY sets a flag; later `cb_mode = 0` (no HT40). The flag lives at byte join_req+0xd1c = `addIEAssoc.addIEdata[1042]` (inside the assoc IE buffer: reproduced as a macro; if an assoc IE is longer than 1042 bytes the stock overwrites an IE byte). lim_fill_pe_session 9544 -> 9704 B (stock 9720; 4 insns left).
34 hdd_config_data_stall_param: stock prints "Set data stall params from use space start" / "... fail" (missing attribute -> message and return 0), params at error level: 336 B identical.
35+41 latency level 5 (Xiaomi): hdd_config_latency_level accepts 1..5; 5 goes straight to sme_set_wlm_latency_level(.., 5, 0, false) without host bookkeeping; sme_set_wlm_latency_level maps host level 5 to firmware level 3 with flags 0: both functions byte-identical in the lab (648 / 364 B).
36 __wlan_hdd_cfg80211_scan: with adapter->scan_ext_flag == 1 and latency level 2/3 the scan first leaves the low latency mode (sme_set_wlm_latency_level(.., 0, 0, 0)); counterpart of hdd_scan_event_callback (g4o).
37 wlan_hdd_add_key_mlo_vdev: pairwise key of a STA adapter whose peer is a TDLS peer: ucfg_tdls_is_key_install_allowed() decides ("TDLS peer's key install disallowed", return 0); the function existed without a caller; prototype added to wlan_tdls_ucfg_api.h.
38 __wlan_hdd_cfg80211_get_preferred_freq_list: flush_work of adapter->ipv4_notifier_work and ipv6_notifier_work (netdev priv +0x30/+0x60) right after the enter print.
39 lim_check_and_announce_join_success prints "Beacon" or "PR": 1128 B identical.
40 ll_lt_sap_high_ap_availability_ser_cb: debug print "reason %d" first: 256 B identical.
42 hdd_soc_probe: hdd_start_complete(0) after trans_stop, hdd_start_complete(errno) before osif_psoc_sync_unregister (wlan_start_ret_val / complete_all in the stock probe path).
43 hdd_sysfs_create_driver_root_obj: no `return` after a failed p2phc kobject (stock falls through and creates "wlan" with a NULL parent; OOM only). Bug-compatible.
44 cm_csr_handle_join_req: mgmt_txrx_frame_hex_dump of the scan entry's raw frame when the bss descriptor cannot be built: 636 B identical.
45 generate_pmkid: an all-zero PMKSA PMKID is neither counted nor copied; "vdev:%d PMKID is 0" when nothing remains: wlan_crypto_build_rsnie_with_pmksa identical except the cross-TU inline of wlan_crypto_get_random_pmkid.
46 lim_fill_rsn_ie: with a PSK-based AKM (PSK, FT_PSK, PSK_SHA256, FT_PSK_SHA384, PSK_SHA384 = mask 0x06000142) the PMKID of the found PMKSA is cleared ("WPA2 does not support PMKID"); wlan_crypto_get_param(KEY_MGMT) is evaluated for both branches as in the stock.
47 __wlan_hdd_cfg80211_get_usable_channel: debug "usable channel count : %d".
48 hdd_post_get_apf_capabilities_rsp: an APF version >= 6001 is reported to user space as 4 ("APF Version ret: %u -> 4", error level); hdd_ctx->apf_version keeps the real value.
49 if_mgr_validate_candidate: second wlan_p2p_is_vdev_wfd_r2_mode() test inside the AP-assist block.
50 target_if_dbr_init_ring: "enter" print + "dbr_psoc_obj %pK" debug print.
51 wlan_hdd_update_rssi: with rssi 0 and a force-inactive vdev (policy_mgr_vdev_is_force_inactive(psoc of the vdev, vdev_id)) rssi = -96, snr = 0, no firmware query.
52 removed (newer CAF, not in the stock): scm_scan_start_req "skip dfs ch ..." print (4536 B identical now), scm_add_rnr_channel_db "skip nontx freq ..." print.
53 removed the DP_TX_DESC_FLAG_REAPED duplicate-entry check ("Txdesc duplicate entry") in dp_tx_comp_handler and dp_tx_comp_handler_be (dp_tx_comp_handler 3244 -> 3168 B = stock; the rh variant dp_rh_tx.c was left alone).
54 target_if_dbr_deinit_ring as in the older CAF of the stock: "dbr_psoc_obj %pK" print, hal_srng_cleanup unconditional (no cds_is_target_ready check, no "SRNG is NULL" message): bug-compatible, hal_srng_cleanup(NULL srng) is possible in the stock.
55 __wlan_hdd_cfg80211_ll_lt_sap_high_ap_availability: second hdd_is_chan_switch_in_progress() after the connection-in-progress block returns -EBUSY WITHOUT releasing the vdev reference: stock bug, kept (the stock has the same leak).

## Left (written down, not recovered; max 3 iterations per function)
- lim_send_assoc_req_mgmt_frame (-56 B): the stock has a different (newer CAF) assoc-request debug message "Assoc Req IEs: dot11mode %d, extcap %d, open %d, IE len:: mbo %d vendor %d, rsnx %d, mscs %d, rsn_sel %d, ft11r %d, mlo %d, eht %d, fils %d" (12 args) instead of ours (extr_ext_flag ... he with tkip ...), two more qdf_mem_copy, and `lim_get_vdev_rmf_capable` / "Peer doesn't support PMF, Don't add MBO IE" + aead_encrypt_assoc_req are called AFTER the frame is built (ours: before). Argument mapping of the 12 values not resolved.
- lim_sta_send_add_bss (-172 B), lim_update_sta_run_time_ht_switch_chnl_params (-64 B): the stock reads a pe_session byte at +0x300a ("Preventing HT40 switch for IoT AP on vdev %d", 2.4 GHz, returns before the HT info is applied) and has the older flow (lim_delete_tdls_peers without lim_update_tdls_set_state_for_fw); our struct pe_session has start_bss_rnr_ie[] at that offset (12264..12320), the stock layout there is different (extra bools 0x300a, 0x300e): the writer of the flag was not found. Probably the pe_session layout of the stock differs from ours after ~0x2fe8.
- policy_mgr_allow_4th_new_freq (-548 B), do_acs frame (+16 B), dlhc_uncompress / dlhc_remember: unchanged from g4p.
- wlan_hdd_cfg80211_get_preferred_freq_list (2012 vs 1968 B): flush added, the rest is the inlined policy_mgr_is_beaconing_mode and a different loop split.
- qca6750 / wcn7750 only-ours hdd_disable_wifi (452 B) and hdd_get_wlan_driver_status (12 B): not investigated.
- populate_dot11f_ht_caps (-32 B), sap_fsm, scm_filter_match, hdd_wmm_select_queue (+28 B in the stock), wlan_hdd_cfg80211_start_bss etc.: call/string multisets equal (seq.sh), differences inside.
- objects: nss_mode_type, p2phc_hooks content, opcls_*_cfis_lst (16 B, kCFI hash tables), jump-table contents, .altinstructions 1 entry, bug-lines (161 of 195, __LINE__ = hack only).

## Hacks / unverified (additions to g4p)
- xm_compat_6657.h emulates a kernel-header vintage (see 1): NOT restored Xiaomi code; it changes the semantics of netif_stop_queue/stop_subqueue/tx_disable (no trans_start) and makes readl/writel call the log_* helpers unconditionally.
- The #line hack (`dump/lh_b11.json`, regenerated by final_r.sh) is NOT restored code; every "with hack" number depends on it.
- HT40-IoT flag at addIEAssoc.addIEdata[1042] (offset taken from the stock, layout of the real struct unknown); level 5 / APF version 6001 / PSK PMKID mask / AKM bits / -96 RSSI: constants and semantics read from the disassembly only; strings are the stock's.
- Bug-compatible stock behaviours kept on purpose: specs 43 (kobject fall-through), 54 (unconditional hal_srng_cleanup), 55 (vdev reference leak). A later review may want to fix them.
- NOT hardware-tested; only structurally compared. The b11 peach_v2 build and the other four b11x builds are the no-hack numbers; h12 are with the hack.

## Next 3 targets
1. pe_session layout after ~0x2fe8 (flags +0x300a/+0x300e): find the writer in lim_check_oui / lim_fill_pe_session (stock) and add the field; then lim_sta_send_add_bss (-172 B), lim_update_sta_run_time_ht_switch_chnl_params (-64 B), lim_send_assoc_req_mgmt_frame (12-argument message, PMF check order, 2 copies).
2. policy_mgr_allow_4th_new_freq (two unrolled copies of if_max_two_home_channel_used, -548 B) and the do_acs frame (+16 B), then hdd_disable_wifi / hdd_get_wlan_driver_status (only-ours in qca6750 / wcn7750).
3. Objects and tables: p2phc_hooks content, opcls cfis (kCFI hashes of 6 prototypes), nss_mode_type, __jump_table / altinstructions, bug-lines; run preamble_cmp on all five builds.
