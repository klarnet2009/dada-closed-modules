# g4s notes (18.10.2026) - Wi-Fi round 9, DONE (PARTIAL x5, no EQUIVALENT)
Start state = g4r package (specs 56-61) copied to <work-dir> scripts = g4r scripts with paths renamed; tools in work/a17/g4s/tools (ss.sh = run a script on the server with retry, up.sh = apply spec, lab.sh = lab compile+compare of one file, ldiff.sh/fdr.sh/rdiff.sh/calls.sh = register-agnostic diffs and call-sequence diffs stock vs lab/real, cls.sh = classify a real build, final_s.sh = final chain).
Replay: g4r/replay.sh, then python3 ../g4o/tools/ed.py <pkg>/qcacld3 specs/6[2-9]_*.spec specs/7*.spec specs/8*.spec (numeric order; 74/75 do not exist).
NOTE: the lab (-Wno-unused-function etc.) does not catch -Werror problems (spec 70 first left an unused static function): always check with a real build.

## Findings (real behaviour differences found in the stock disassembly; all spec files in specs/)
62-64 policy_mgr_allow_4th_new_freq (stock 3968 B): if_max_two_home_channel_used compares ALL 10 pairs of the 5 freq slots (bounds = MAX_NUMBER_OF_CONC_CONNECTIONS, not mac_freq_num) and the stock has two unrolled copies (case 3, case 4 separate); plus a missing tail clause: after the two NAN clauses it returns true when sta == 1 || (sta == 2 && p2p == 2) || p2p == 3 (stock +0xd30). Real build 3648 -> 3928 B.
65 policy_mgr_dfs_master_cfg_changed: sta_sap_scc_on_dfs_chnl cleared only when value == 2 (FLEX) and not dfs master capable (was: always cleared when not capable).
66 policy_mgr_check_scc_channel: returns right after the pm_ctx check when the HW is not DBS (was: continued when a STA existed). Byte-identical.
67 policy_mgr_get_pcl: dump_channel_list(*len) right after policy_mgr_modify_pcl_based_on_dnbs, before the status check. Byte-identical.
68 hdd_wmm_get_user_priority_from_ip_tos: when tos == 0 and skb->priority is 1..7 the user priority = skb->priority (missing in our source).
69 scm_ignore_ssid_check_for_hidden_bss: bssid-hint match tested only if hint != 0 or key_mgmt has OWE (bit 22).
70 __scm_handle_bcn_probe: the stock has no scm_skip_bcn_ch_mismatch_check_by_oui (drop on channel mismatch unconditionally): call and function removed.
71 sap_check_and_update_vdev_ch_params message "updated BW %d -> %d" without vdev id.
72 pmo_get_apf_instruction_size: PMO_APF_SIZE_AUTO is 0x800 (was 0): the function returned 0 instead of 0x800 when APF is enabled.
73 p2phc_p2px_event: hooked=true before the info message (stock order). Layout of p2phc_device_event still differs (2 trials reverted: plain static, swapped branches).
76 csr_fill_cmd_timeout: 12000 ms for VDEV_START_BSS and 17000 ms for VDEV_STOP_BSS (stock lookup table decoded: rest matched). Byte-identical.
77+84 reg_skip_invalid_chan_freq: with exactly one connection its freq is remembered ("allow sap/go to use freq %u") and an indoor channel is removed for SAP/GO unless it is that freq AND indoor channels are supported (was: removed only when indoor channels unsupported); reg_update_list_for_dfs_channel tests only the DFS flag (no 5 GHz range check, no indoor test).
78 sir_convert_assoc_resp_frame2_struct: ML partner info validated/copied without testing status_code == SUCCESS.
79 dp_prealloc_init: for QDF_DP_TX_DESC_TYPE the element size is rewritten to 0x80 together with the element count (dp_update_num_elements_by_desc_type got an element_size out argument). Byte-identical.
80 scm_add_rnr_channel_db: no "skip non Tx MBSSID profile" test.
81 __wlan_hdd_hostapd_start_bss: abort of the non-SAP scans is unconditional (no policy_mgr_is_vdev_ll_lt_sap test).
82 __dlhc_init: kzalloc (flags 0xdc0) instead of kmalloc. Byte-identical.
83 __wlan_hdd_cfg80211_wifi_configuration_set: no NULL test of the link_info found by link id (bug-compatible).
85 policy_mgr_cfg.mi_sap_only_allow_sta_dfs_indoor_chan is uint8_t (stock stores the raw INI byte, normalises on read).
86 __hdd_cm_disconnect_handler_post_user_update: no hdd_wmm_adapter_clear before hdd_adapter_reset_station_ctx.
87 wlan_hdd_sap_p2p_11ac_overrides: 5/6 GHz branch overrides ch_width_orig only when it is <= 40 MHz. wlan_hdd_setup_driver_overrides byte-identical.
88 wlan_hdd_populate_weigh_pcl: returns right after the PCL entries for P2P client/GO ("Do not add MCC or unsupported ch in p2p prefer list").
89 hdd_check_ap_assist_dfs_group_start_req: re-checks p2p_is_vdev_wfd_r2_mode(vdev) first and returns success when it is not set.

## Result (real Kleaf builds, fcmp: byte-identical functions of N stock functions WITHOUT / WITH the #line hack; g4r in brackets)
| variant | N | no hack | with hack | only-stock | only-ours |
| peach_v2 | 18446 | 17814 [17804] | 18302 [18289] | 0 | 0 |
| peach | 18324 | 17413 [17404] | 17892 [17880] | 0 | 0 |
| kiwi_v2 | 18271 | 17645 [17635] | 18126 [18113] | 0 | 0 |
| qca6750 | 15880 | 15298 [15288] | 15752 [15739] | 0 | 0 |
| wcn7750 | 17981 | 17357 [17347] | 17841 [17828] | 0 | 0 |
peach_v2 without hack (offpairs2): STRUCT 38 -> 17, OTHER 7 -> 8, IMM 593 -> 601 (= functions whose only change is now __LINE__, they become identical with the hack), REG 3 -> 5, MISSING 1 (name .llvm suffix only).
compare_ko_v2 (peach_v2, hack build, logs/compare_ko_v2_peach_v2_hack.txt): NOT STRICT-EQUIVALENT. PASS: function-set, function-binding, kcfi-preamble 17008/17008, unowned-code, exports, imports 584+CRC, modinfo, buildmeta, depends, vermagic, notes. FAIL: function-bytes 144 of 18601 [157], function-relocs 173 [191], objects (p2phc_hooks + opcls_131..137_cfis_lst + 15 more: relocation-target layout), anon-data (stock-only 17 [19]), bug-table + bug-lines (161 of 195, real source line numbers), jump-table (48=48, contents: tso_info / dp_rx_process_be / dp_tx_comp_handler scheduling), .altinstructions 5390 vs 5391 (dp_tx_comp_handler). No-hack build: function-bytes 632, bug-lines 162 (logs/compare_ko_v2_peach_v2_nohack.txt).
preamble_cmp.py (kCFI hash words) on all five hack builds: 0 mismatches (16958 / 16839 / 16800 / 14744 / 16542 functions).
Hacks: #line hack (logs/lh_sf.json, regenerated for this source state, NOT restored code), xm_compat_6657.h (build-environment emulation of the 6.6.57 headers), bug-compatible items (dead pe_session flag, HT-only width cap, no force_scc [g4r]; NULL test removed in wifi_configuration_set (83), ll_lt_sap guard removed in start_bss (81), OUI channel-mismatch feature removed (70), tos==0 skb->priority rule (68)): need review. NOT hardware-tested.

## Left (not recovered)
- 17 STRUCT functions without the hack are codegen only (same call sequences): lim_fill_pe_session -16 B, do_acs +44 B, policy_mgr_allow_4th_new_freq 3928 vs 3968 B (spill slots), cm_process_join_req, update_wiphy, dp_rx_process_be, target_if_direct_buf_rx_module_register, wlan_hdd_cm_connect, dp_tx_comp_handler(_be), lim_send_assoc_req_mgmt_frame, tso_info, link_peer_stats_sap, add_scan_entry, dlhc_remember, target_if_deinit_dbr_ring, netif_queue_control; OTHER/REG: scheduling.
- objects/tables: p2phc_hooks and opcls cfis (relocation layout), anon-data 17, jump-table/altinstructions (follow the scheduling diffs), bug-lines (real line numbers).

## Next 3 targets
1. Block placement / spill slots with __builtin_expect_with_probability and local-variable order for the 17 STRUCT + 8 OTHER functions (dlhc_remember/uncompress/init, p2phc_device_event, allow_new_home_channel: placement of the rate-limited debug print), then the jump-table/altinstructions entries follow.
2. Review of the behaviour changes of this round (esp. 65, 68, 77/84, 81, 83, 70) by an independent reader against the stock; look for further such differences in the REG/IMM groups with the same method (call-sequence diff tools/calls.sh + immediate multiset diff).
3. Layout-driven object diffs (p2phc_hooks, opcls cfis, anon-data) and real source line numbers for bug-lines.

## Not recovered (codegen only, 3 iterations used)
- wlan_hdd_cfg80211_do_acs: +16 B frame (stock has additional spill slots); lim_fill_pe_session -16 B (scheduling / address materialisation); cm_process_join_req (stock duplicates the RSN-IE tail: same calls); dlhc_remember / dlhc_uncompress / dlhc_init (block placement of error paths; positive-form check did not change anything); p2phc_device_event (block order); policy_mgr_allow_new_home_channel (placement of the rate-limited debug print); cfg_parse (same immediates, different scheduling); many functions with only scheduling/register differences.
- Leftover IMM functions with the hack are __LINE__ values inside files where the stock numbering cannot be reached without the real source (bug-lines).
