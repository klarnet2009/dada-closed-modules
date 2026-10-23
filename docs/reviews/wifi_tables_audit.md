# qca_cld3_peach_v2: data-table audit and STRUCT-function review (goal-G22f, 20.10.2026)

Reference = GLOBAL OS4.0.0.7 stock `<work-dir>` (md5 59d72a62...).
Ours = real Kleaf rebuild of the unchanged g4u package, no `#line` hack: `<work-dir>` (md5 d763db24..., = base_peach_v2.ko);
for the instruction-level part also `<work-dir>` (same package + `#line` hack, 133 differing functions).
Scratch `<work-dir>` (no bazel-out, nothing rebuilt). Tools (copies in `work/re/src/g22f_tools/`): `tbl_audit.py`, `anon_pair2.py`, `anon_find.py`, `imm_ctx_all.py`
(built on compare_ko_v2.py `Elf`: relocation fields resolved to the target symbol/content, so a pointer table is compared by what it points to).

## 1. Data objects (content, not size)

All 2828 named data objects (2095 distinct names; 2 308 316 bytes) were compared per normalised name as multisets of (class, size, content + resolved relocation targets).
Only 8 names differ; every other object is byte+relocation identical. Section totals differ only by alignment padding (.rodata+str+cst 2 027 603 vs 2 028 816, .data 407 341 vs 407 472, .bss 1 480 985 vs 1 481 328: ours is per-object sections before the final link, stock is merged).

| class (regex on object names) | names | bytes | equal | differ |
|---|---|---|---|---|
| op-class / channel lists (`opcls`, `op_class`, `channel`, `chan_*`) | 118 | 29 812 | 110 | 8 (below) |
| regdomain / country (`reg*`, `country`, `cc_`, `dfs`, `regdomains_5g/2g`, country tables) | 125 | 98 211 | 125 | 0 |
| PCL / policy_mgr (`pm_*_pcl_*_table`, `second/third/fourth_connection_pcl_*`, ...) | 219 | 122 144 | 212 | 7 (same 7 opcls objects, regex overlap) |
| power / tpc / eirp / psd | 53 | 2 074 | 53 | 0 |
| roaming / 11k / 11v / 11r (`rrm`, `btm`, `neighbor`, `rssi`, `threshold`, `ft_`, `mdie`) | 68 | 15 686 | 68 | 0 |
| cfg / ini default tables (`cfg_*`, `*_cfg`, `default`) | 139 | 56 129 | 139 | 0 |
| rate / mcs / nss / he / vht (`supported_*_rate_nss*`, `mcs_nss*`) | 72 | 137 379 | 72 | 0 |
| all `.rodata` objects >= 64 B | 397 | 411 412 | 396 | 1 (`global_op_class`) |

The 8 differing objects, all PLACEMENT ARTEFACTS (same bytes, different address), no content difference:
- `opcls_131..137_cfis_lst` (7 objects, 16 B each): `{num = 0, ptr = opcls_13x_cfis_arr}`; word +0/+4 equal (0), only the relocation at +8 differs: stock points at `.rodata+0x190180` (whatever object follows the zero-length array), ours at the zero-size section `.rodata.opcls_13x_cfis_arr`. The arrays are empty in both (6 GHz CFI arrays are under `#ifdef CONFIG_AFC_SUPPORT`, off in both; num = 0 so the pointer is never dereferenced).
- `global_op_class` (2592 B): 7 hunks / 14 lines, each the pointer word of the 7 entries 131..137 to the above lists (+0x778, 0x7d8, 0x838, 0x898, 0x8f8, 0x958, 0x9b8); every other word (op class number, spacing, bandwidth, channel arrays, flags, the 118 other classes) identical.

Anonymous (unnamed) data: compare_ko_v2 reports 17 stock-only / 19 ours-only chunks. All 36 resolved as chunk-boundary / placement artefacts: every non-zero byte of every chunk occurs as a long match in the other module (greedy longest-match over the full token stream of all data sections, relocation fields replaced by the resolved target; the two known placement pairs above normalised): the `switch` lookup tables of qdf_rtpm_put, cm_connect_complete_ind, lim_fill_lim_assoc_ind_params, target_if_pdev_spectral_init (4), osif TWT setup/teardown/pause/resume/nudge (5), wma_convert_emlsr_tran_delay, reg_get_class_from_country (5 ptrs), reg_chan_opclass_to_freq_auto (6 ptrs), wlansap_set_channel_change_with_csa (16 string ptrs), sme_trace_dump (92 string ptrs), wlan_ipa_uc_info (13 ptrs) are the same bytes in stock `.rodata+0x1ea17c..0x1ef15c` and in ours as separate `.rodata..Lswitch.table.*`; the 6 tiny stock blobs (`50 6f 9a 1c/10/16`, `50 6f 9a 01`, `7f 50 6f 9a 09`, `04 0b`, `04 0c`, `0a 07`, `0a 1a`: OUI/IE id constants) are in ours as `.rodata.str1.1` strings or inside larger arrays. Weak point: the 2-5 byte blobs prove only that the bytes exist, not that the same code reads them; the functions that use them are byte-identical or LINE-only (below).

Data that the stock fills with code (ini defaults `cfg_*_parse`, `wlan_cm_rso_config_init`, `wlan_scan_init_default_params`, `mlme_cfg_on_psoc_enable`, PCL/policy init): such functions are either byte-identical (the 17825 functions without hack, 18313 with hack) or among the 133 with-hack differences. For those 133, EVERY differing `mov/movk` immediate (stock-only vs ours-only, all pairs, not only the large-delta ones as in g15a) was resolved to the call it feeds (`imm_ctx_all.py`): 265 `__qdf_mem_malloc`, 78 `qdf_trace_msg`, 22 `qdf_mem_malloc_atomic_fl`, 18 `scheduler_post_message_debug`, 10 `__cds_trigger_recovery`, `dp_ipa_handle_rx_buf_smmu_mapping`, `wmi_buf_alloc_fl`, `*_alloc_consistent`, `qdf_mutex_create`, `qdf_aligned_malloc_fl`, `cm_get_cm_ctx_fl`, `wlan_cm_get_rso_config_fl`, `wmi_unified_cmd_send_fl`, `_printk` = line-number arguments only. Exceptions looked at one by one: `__qdf_nbuf_get_tso_info` `-0x100000000000000` (page-to-phys mask, see 2), `cm_process_join_req` 0x35/0x7/0x8 (the inlined second copy of lim_fill_rsn_ie, see 2), `lim_fill_pe_session` 0x11c6 (line at a scm call), `vendor_add_scan_entry` 0x1 (cinc), none is a default/threshold/regulatory constant. So no ini default, roaming threshold or regdomain constant differs from the stock.

Verdict part 1: 0 content differences, 8 + 36 placement artefacts.

## 2. The 13 STRUCT functions (stock vs ours, register-masked multiset diff on the hack build + symbolic facts + path pairing in the asm)

| function | result |
|---|---|
| lim_fill_pe_session | NOTE: stock has 3 calls of wlan_cm_set_disable_hi_rssi (+0x1dcc, +0x1e30, +0x1e44), OURS has 2 (briefing had it reversed). Source: A `self_mld_roam` -> false, B `24GHz && abs(rssi) > th-delta` -> log "Enabling HI_RSSI" then false, C else -> true then log "Disabling". Stock: A set(false); B log, set(false); C set(true), log. Ours: A and B share one set(false) (A jumps over the log, B logs and falls in), C set(true), log. Per path: same calls, same argument values, same order of log vs set; same merge point. EQUAL. The remaining 35 multiset diffs are stack-slot ([SP,#..]) and branch-sense only. |
| cm_process_join_req | stock inlines lim_fill_rsn_ie twice (non-NULL pmksa and NULL pmksa tail), ours once with the pmksa as a phi. Stock non-NULL: "PMKSA found", get_param(KEY_MGMT), if akm & PSK mask log + qdf_mem_set(pmkid,0,16), build_rsnie_with_pmksa(.., pmksa), fail -> "Build RSN IE failed", len store, qdf_mem_copy, free, wlan_cm_set_psk_pmk, lim_update_pmksa_to_profile. Stock NULL: get_param (result dead), build_rsnie_with_pmksa(.., NULL), fail log, len+copy, free, no set_psk_pmk. = source order, = ours. EQUAL. |
| dp_rx_process_be | ONE PROVEN BEHAVIOUR DIFFERENCE: ours has an extra `ldrb [nbuf+0x31]; tst #0x3e; b.ne` in the inlined `dp_rx_cksum_offload` = `dp_check_rx_from_reo_excpt(nbuf)` (dp_rx.h:2548, newer CAF: "mark all packets from the FW offload path as unchecked"); stock has no such test (no `tst #0x3e`, no `[+0x31]` load anywhere in the function). Effect: packets re-injected by FW offload (sw_exception bits set) skip the HW checksum result in ours (CHECKSUM_NONE, software verification, small cost), stock marks them UNNECESSARY. No safety impact. Not fixed (needs one rebuild, budget reached); fix = delete the two lines `if (qdf_unlikely(dp_check_rx_from_reo_excpt(nbuf))) goto bypass_tcp_udp;` in cmn/dp/wifi3.0/dp_rx.h. Rest: only the __FILE__ path string of an assertion. |
| dp_tx_comp_handler, dp_tx_comp_handler_be | `cbnz`/`cbz`+`b` swap and the __FILE__ path of the assertion; symbolic facts equal. EQUAL. |
| __qdf_nbuf_get_tso_info | ours has one extra NULL test of the frag page (`cbz x11`) in the inlined page->phys of the 6.6.144 headers; stock (6.6.57 headers) computes phys from a NULL page without test (and then trips the `paddr` assertion). Equal for valid skbs; header vintage, cannot be changed from the driver. |
| lim_send_assoc_req_mgmt_frame | MBO OUI literal as anonymous blob (stock) vs merged string (ours), same 4 bytes and length; `ldp` vs two `ldr`. EQUAL. |
| hdd_open_p2p_interface | `ldp/stp` vs `ldr/str` of the same stack slot. EQUAL. |
| target_if_direct_buf_rx_module_register | stack-arg and `__func__` of two inlined log lines. EQUAL. |
| wlan_hdd_cfg80211_vendor_add_scan_entry | bandwidth default branch: both call reg_is_24ghz_ch_freq(freq): stock `tbz -> 1 / 2`, ours `cinc` (1 + !is24); same value 1|2|3 set. Other diffs are the stack-frame size (0x50 vs 0x40). EQUAL. |
| policy_mgr_allow_4th_new_freq | commutative swap of the two `== 2` tests of a conjunction (ccmp order) and polarity of one `cset`; duplicated blocks; stack slots. EQUAL by facts (not by bytes). |
| wlan_hdd_update_wiphy | policy_mgr_find_if_fw_supports_dbs (pure query) executed twice in ours (jump threading), once in stock; the guarded stores are the same; frame 0x130 vs 0x140. EQUAL. |
| wlan_hdd_cfg80211_do_acs | all compare constants equal after normalising `cmp #N; b.hi` vs `cmp #N+1; b.hs` and branch polarity, except the placement of the UBSAN bounds trap of stock bug S4 (stock traps per element at `idx*4 > 0x197` inside the copy loop, ours checks `count > 102` once before it). Valid inputs (count <= 101) behave the same; the difference exists only on the already-panicking invalid path. Frame-slot sharing explains the 11-instruction stack differences. EQUAL for valid input; NOT byte-proven. |

Result part 2: 12 of 13 equal, 1 proven behaviour difference (dp_rx_cksum_offload FW-offload early-out), 0 fixes applied (no rebuild: budget). Counts unchanged: 17825/18446 without hack, 18313 with.

## 3. Next targets
1. Remove `dp_check_rx_from_reo_excpt` early-out in dp_rx.h:2548 (g4u package copy `<work-dir>`), ONE real Kleaf rebuild, expect dp_rx_process_be to drop its 3 extra instructions.
2. do_acs / policy_mgr_allow_4th_new_freq / lim_fill_pe_session byte-level: try small source reshapes (single-use locals, `else` order) to remove the remaining stack-slot/phi differences, then compare bytes.
3. Hardware: DUT test of FW-offload RX checksum (iperf over P2P/SAP) and roaming (11k/v/r) on the global OS4 base once flashed; the static audit cannot see FW-side behaviour.
