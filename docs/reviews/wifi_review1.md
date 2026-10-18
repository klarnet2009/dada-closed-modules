# Wi-Fi qca_cld3_* review 1 (review-wifi-opus1, 18.10.2026)

Scope: package `<work-dir>` (= g4r + specs 62-89), builds `<work-dir>` (with #line hack) and
`sf_peach_v2.ko`, stock `<work-dir>`. Reviewer only, no source edits, no Bazel build.
Scratch: `<work-dir>` (bin/ = compare_ko_v2.py, symcmp.py, fd.py, **objx.py** (named-object dump/diff with relocations resolved),
**symlm.py** (symcmp with `__LINE__` masked before hashing), evidx.py (vendor-event index per call site); outputs `cmp_sh_v.txt`,
`sym_all.txt`, `symlm_all.txt`, `ours_sh.dis`). Local copies of the filters: scratchpad `r6/` (symfilt.py, lmfilt.py).

Method: (a) compare_ko_v2 -v on peach_v2 (hack build): 144 functions with different bytes; (b) symbolic compare of all 144 with line
masking: 105 equal modulo __LINE__/paths, 39 left, all looked at (34 after path/phi masking); (c) the `objects` FAIL list (21 objects)
dumped word by word in both modules (it had been labelled "relocation layout" since g4o: 14 of the 21 are REAL content differences);
(d) each deliberate change of g4r/g4s checked against the stock disassembly; (e) every finding re-checked on peach, kiwi_v2, qca6750, wcn7750.

## Findings, ranked (stock = reference; "all 5" = present in all five variants, shared source)

### F1 HIGH (function broken, all 5) p2phc netfilter hook registered at the wrong hook point
- `xiaomi_ext/p2phc_core.c:30-38`: `.pf = NFPROTO_IPV4, .hooknum = NF_NETDEV_INGRESS` (= 0 = NF_INET_PRE_ROUTING for pf IPv4).
- Stock object `p2phc_hooks` (nf_hook_ops, 40 B): +0x18 pf = **1 (NFPROTO_INET)**, +0x1c hooknum = **5 (NF_INET_INGRESS)**, priority INT_MIN;
  ours pf = 2, hooknum = 0 (objx.py p2phc_hooks, identical result in all five variants). g4r NOTES line 24 "contents equal" is wrong.
- Failure scenario: stock hooks the per-device inet ingress of the p2p netdev (before the IP layer). Ours is a global IPv4 PRE_ROUTING hook
  that runs after `ip_rcv_core()`, which drops every packet whose version nibble is not 4. The compressed/uncompressed-with-cid frames of
  the peer carry version 7/8/9/a, so they never reach `rx_inet_hook`: with p2phc enabled on the peer (Xiaomi-to-Xiaomi P2P, e.g. cast /
  file share), every compressed TCP/UDP packet is dropped and the P2P connection stalls. The `.dev` set in `p2phc_p2px_event` is ignored, and
  the hook now runs (strncmp of dev->name) for every IPv4 packet of every interface.
- Security note: today the remote-input parsers `dlhc_uncompress`/`dlhc_remember` are unreachable (smaller attack surface than stock). Once
  pf/hooknum are fixed to stock values they become reachable from any P2P peer again (see stock bugs S1-S3); our dlhc_* are symbolically
  equal to stock (incl. the fortify field-spanning check in dlhc_remember), so the fix restores stock behaviour, no more.

### F2 MEDIUM (behaviour, all 5) policy_mgr PCL tables differ from the Xiaomi stock
- 90 table words differ: pm_second_connection_pcl_dbs_sbs_2x2_table 6, pm_second_connection_pcl_dbs_2x2_table 12,
  pm_third_connection_pcl_dbs_2x2_table 42, second_connection_pcl_nodbs_table 6, fourth_connection_pcl_dbs_sbs_table 24.
  Examples (enum policy_mgr_pcl_type): pm_second_connection_pcl_dbs_2x2_table +0x264..0x278 stock 9 (PM_5G_SCC_CH) x6, ours 27
  (PM_24G_SCC_CH_SBS_CH) / 1 (PM_24G); pm_third_connection_pcl_dbs_2x2_table +0x600..0x614 stock 7 (PM_SCC_CH_5G), ours 1 (PM_24G);
  pm_second_connection_pcl_dbs_sbs_2x2_table +0xb4/+0xc0/+0xcc stock 6 (PM_SCC_CH_24G), ours 31/33.
- Scenario: channel/band preference of the 2nd/3rd/4th connection (SAP, GO, P2P) in concurrency differs: stock prefers 5 GHz SCC where ours
  picks 2.4 GHz (lower hotspot/P2P throughput, different MCC/SCC decisions). No memory risk (same table sizes).

### F3 MEDIUM (behaviour, all 5) interface combination STA+P2P+P2P smaller than stock
- `core/hdd/src/wlan_hdd_cfg80211.c:745` wlan_hdd_sta_p2p_p2p_iface_limit: stock {1 STATION, **3** P2P_CLIENT|P2P_GO}, ours {1, 2};
  combination entry (wlan_hdd_iface_combination +0x38): stock num_different_channels **3**, max_interfaces **4**; ours 2 / 3.
- Scenario: cfg80211 rejects a third P2P interface / 3-channel concurrency that the stock driver advertises (Xiaomi multi-P2P features fail).

### F4 MEDIUM (behaviour, all 5) INI default of mi_sap_only_allow_sta_dfs_indoor_chan
- `components/cmn_services/policy_mgr/inc/cfg_policy_mgr.h:750` CFG_MI_SAP_ONLY_ALLOW_STA_DFS_INDOOR_CHAN default 0.
- Stock cfg_parse peach_v2 +0x9d8 (`mov x25,#2; movk x25,#1,lsl#32` at +0xfc/+0x108; `str x25,[store+0xaa4]`): STA_SAP_SCC_ON_DFS_CHAN = 2 and
  the next field (this item) = **1**; ours stores 2/0 (+0x9d4). Same in all five (offsets 2748/2744/2588/2736). Neighbours confirm the
  mapping (0xa84 = 0x4321 vdev_priority_list, 0xa98/0xa9c = 1/6, 0xaa0 enable_sbs, 0xaac sta_sap_scc_on_indoor_chan).
- Scenario: without an explicit INI line the SAP-on-STA-DFS/indoor-channel rule (the Xiaomi rule behind specs 77/84/85) is OFF in ours, ON in stock.

### F5 LOW-MEDIUM (behaviour, all 5) Miracast optimisation timer 5 s instead of 20 s
- `xiaomi_ext/xiaomi_ext.c:368` `qdf_timer_mod(&...miracast_opt_timer, 5000)`; stock wlan_mlme_start_miracast_opt +0x44 `mov w10,#0x1388`,
  `umaddl x1,w9,w10,x8` = jiffies + mult*5000 **jiffies**; ours folds to mult*1250 (HZ=250): the stock value is 20000 ms (matches the 20000 ms
  wake lock two lines below). The agents read the jiffies immediate as milliseconds.

### F6 LOW-MEDIUM (behaviour, all 5) extra condition in the SAP/STA DFS CSA failure path
- `core/hdd/src/wlan_hdd_cm_connect.c:835` `if (ret && qdf_atomic_read(&hdd_ap_ctx->ch_switch_in_progress) <= 1)`; the stock
  (wlan_hdd_cm_connect, inlined wlan_hdd_cm_handle_sap_sta_dfs_conc) has no atomic read (symlm: ours-only `ldar [ap_ctx+2636]`, `< 2`).
- Scenario: a failed CSA while another switch is in progress makes ours wait up to 10 s on the hostapd event and possibly allow the STA,
  stock refuses the connect at once.

### F7 LOW (all 5) interdependent_setters one entry short
- `wlan_hdd_cfg80211.c:14797`: stock table 20 entries (hdd_config_data_stall_param twice: before hdd_config_tx_rx_nss_per_band and last),
  ours 19; stock loop bound `cmp x21,#0xa0` at wlan_hdd_cfg80211_wifi_configuration_set +0x250, ours #0x98. Data-stall setter runs once,
  after the NSS setters; probably idempotent, ordering differs.

### F8 LOW (all 5) vendor event table: 76 in stock, 75 in ours
- wlan_hdd_cfg80211_vendor_events: stock last entry {OUI 0x1374, subcmd 240}; n_vendor_events 0x4c vs 0x4b (wlan_hdd_cfg80211_init store
  a1+1408). All 63 `__cfg80211_alloc_event_skb` sites have identical immediate indices in both (max 0x4a): no sender of index 75 found, so
  only the advertised event list differs (no WARN path found).

### F9 LOW (peach_v2, peach only) vendor command 251 flags
- hdd_wiphy_vendor_commands entry subcmd 0xfb (wlan_hdd_cfg80211_p2p_parse_wfd_params): stock flags 7 (WDEV|NETDEV|**RUNNING**), ours 3.
  kiwi_v2/qca6750/wcn7750 stock have 3 (equal). Ours accepts the command on a down interface on the two peach variants.

### F10 LOW (all 5, ours safer) target_if_deinit_dbr_ring is newer CAF, not "codegen only"
- Ours unmaps only when paddr != 0, clears paddr and sets ring_cfg->srng = NULL after hal_srng_cleanup; stock (+0x1ec..0x248) unmaps
  every buffer unconditionally and frees with explicit NULL tests. No crash risk in ours; behaviour differs from the reference.

### F11 LOW / cosmetic
- wlan_hdd_get_link_peer_stats_sap: stock also sets sinfo->filled bit 11 (NL80211_STA_INFO_TX_RETRIES): 0x1801A8E vs ours 0x180128E.
- rssi_mcs_tbl: ours 5 rows (280 B), stock 3 (168 B); hdd_report_max_rate is byte-identical and its row index is 0..2 (+0x360..0x380),
  extra rows unused.
- policy_mgr_allow_new_home_channel: one path prints "3rd home channel on same MAC" in stock, "don't allow ..." in ours (same return logic).
- hdd_scan_event_callback "Re-enter low latency mode failed" log level 8 (stock) vs 2; sch_beacon_process "Beacon interval changed from
  %d to %d" arguments swapped; hdd_drv_cmds order (SetScanExtFlag last in stock; no prefix collision, harmless).
- `#line 1926` inside xiaomi_ext.c of the NON-hack package; xm_compat_6657.h disables -Wmacro-redefined for every TU (a real conflicting
  macro redefinition would now pass silently).

## Deliberate / bug-compatible changes: verdict per item (peach_v2, then the other four by shared source)
- spec 83 (NULL test of link_info removed in wifi_configuration_set): CORRECT and harmless. Stock inlines hdd_get_link_info_by_link_id as
  "return adapter->deflink" (+0x174 `ldr x19,[x24]`, x24 = adapter+0xd6d0); ours identical, link_info can never be NULL.
- spec 81 (ll_lt_sap guard removed in start_bss): wlan_hdd_cfg80211_start_ap equal to stock modulo __LINE__ (symbolic). Worst case: a LL_LT
  SAP start aborts STA scans (retried); no memory effect.
- spec 70 (OUI channel-mismatch skip removed): the removed function already returned false; __scm_handle_bcn_probe byte-identical. No change.
- spec 68 (tos==0 -> skb->priority 1..7): hdd_wmm_select_queue byte-identical; index stays in 0..7 of the 8-entry maps. Stock design note:
  with TOS 0 an app can pick its WMM AC through SO_PRIORITY (unprivileged up to 6).
- specs 77/84 (reg_skip_invalid_chan_freq, DFS-only list update), 65 (dfs_master_cfg_changed): equal to stock modulo __LINE__.
- dead pe_session flag +0x300a, HT-only width cap: readers and lim_sta_send_add_bss byte-identical; flag never written -> HT40 IoT block inert as in stock.
- RSNXE/MSCS in lim_send_assoc_req_mgmt_frame: symbolically equal (length accounting, packed sizes, frees); only difference the MBO OUI literal
  placement (stock anonymous 4 bytes, ours merged string 50 6f 9a 16): same bytes and length 4.
- pmo_get_apf_instruction_size 0x800, csr_set_serialization_params_to_cmd 12/17 s, __dlhc_init kzalloc, wlan_hdd_sap_p2p_11ac_overrides,
  wlan_hdd_populate_weigh_pcl, policy_mgr_check_scc_channel, policy_mgr_get_pcl: byte-identical to stock.
- dp_prealloc_init (TX desc size 0x80): equal modulo __LINE__ (only line args of qdf_aligned_mem_alloc_consistent_fl differ).
- dlhc_* (remote input once F1 is fixed): dlhc_remember/dlhc_uncompress/dlhc_init equal to stock symbolically (bounds tests identical,
  incl. the stock gaps S1). cm_process_join_req: tail duplication only (build_rsnie_with_pmksa(pmksa or NULL) in both).
- wlan_hdd_cfg80211_vendor_add_scan_entry (vendor cmd 240, user input): same validation (nla_len == 4 for freq/bw, BSSID 6, SSID 1..32,
  IE >= 2 and <= 2304 by policy); the bw default branch is equal (stock +0x200..0x214); only stack layout differs.
- xm_compat_6657.h: netif_tx_stop_queue without the trans_start write/barrier = 6.6.57 semantics. The 6.6.144 dev_watchdog only checks
  stopped queues, so a queue stopped through the driver's inlined netif_stop_subqueue/netif_tx_disable after a long idle period can raise
  a false hdd_tx_timeout (log + tx-timeout/data-stall counters) - exactly the stock behaviour on its 6.6.57 kernel; hdd's main pause path
  (netif_tx_stop_all_queues is out-of-line in the 6.6.144 kernel + wlan_hdd_update_txq_timestamp) still refreshes trans_start. readl/writel
  call log_*_mmio unconditionally (cost only). Not harmful beyond stock.
- CONFIG_CNSS_HW_SECURE_DISABLE removed for qca6750/wcn7750: stock modules have neither hdd_disable_wifi nor hdd_get_wlan_driver_status;
  function sets equal; correct.

## kCFI / traps
- kcfi-preamble 17008/17008 PASS (peach_v2), preamble_cmp 0 mismatches on all five (g4s). In the symbolic compare of all 144 differing
  functions no indirect-call type-hash check differs (e.g. both setter tables checked with 0xb6875a9d / 0xa6382bc2); the other 18457
  functions are byte-identical -> no new kCFI trap. rx_inet_hook preamble equal (nf_hookfn).
- UBSAN bounds traps (brk #0x5512) present at the same places in stock and ours (e.g. do_acs fast-ACS arrays, see S4).

## Stock bugs (kept, do NOT fix)
- S1 dlhc_remember (__dlhc_remember_tcp/udp): only `isize >= 20 && ihl >= 5`; copies ihl*4 + 20 (+ (doff-5)*4) bytes and checksums ihl*4
  bytes without checking isize -> out-of-bounds read of skb data into the compressor state (remote P2P peer).
- S2 __rx_inet_hook: `len = skb->len` with the linear network header (non-linear skb -> OOB), `skb_put(skb, 120)` on a possibly non-linear skb
  (SKB_LINEAR_ASSERT BUG); skb not trimmed back to the uncompressed length (relies on ip_rcv tot_len trim).
- S3 the RX hook ignores sysctl_p2phc_enable: any P2P peer drives the dlhc state machine whenever a p2p netdev exists.
- S4 do_acs Xiaomi fast ACS (CN, is_sap_fast_acs, hw_mode >= A): old_2g/old_5g[NUM_CHANNELS-1 = 101] filled from the user ACS list
  (ch_list_count up to 255, duplicates allowed) -> UBSAN trap = kernel panic from a CAP_NET_ADMIN vendor command (stock +0x12bc
  `cmp w10,#0x64; b.hi` -> brk); and acs_cfg.ch_list_count := n (stock +0x1524 `strb w24`) can exceed the size freq_list was allocated with
  -> later heap out-of-bounds read/write by SAP ACS code. Ours identical (wlan_hdd_cfg80211.c:4172-4276, call at 4602).

## Verified equal (peach_v2)
105 of the 144 byte-different functions are symbolically equal after __LINE__/path masking (htt_*, scan core, ucfg_scan_*, reg/wmi
extract_*, sme_*, lim_process_sme_req_messages, dp_tx_comp_handler(_be), dp_rx_*, csr_*, sch_beacon_process except the message arguments, ...).
Not reviewed line by line (left for the next round, only codegen-looking residue): lim_fill_pe_session (-167/+143 facts),
policy_mgr_allow_4th_new_freq (24/25), wlan_hdd_update_wiphy (53/54, contains F3), the rest of wlan_hdd_cfg80211_do_acs (91/62),
policy_mgr_allow_new_home_channel mac-freq-list part, target_if_direct_buf_rx_module_register (1/1), __qdf_nbuf_get_tso_info (ours one
extra NULL test, harmless).
