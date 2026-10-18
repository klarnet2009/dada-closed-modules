# g4q 52: two debug prints of a newer CAF that the stock does not have: scm_scan_start_req "skip dfs ch ..." and scm_add_rnr_channel_db "skip nontx freq ..."
@@@ cmn/umac/scan/core/src/wlan_scan_manager.c
<<<<
	if (skip_dfs_ch) {
		scm_nofl_debug("skip dfs ch allow_dfs_chan_in_scan = %d allow_dfs_chan_in_first_scan = %d first_scan_done = %d skip_dfs_chan_in_p2p_seach = %d p2p_search = %d miracast_enabled = %d",
				scan_obj->scan_def.allow_dfs_chan_in_scan,
				scan_obj->scan_def.allow_dfs_chan_in_first_scan,
				first_scan_done,
				scan_obj->scan_def.skip_dfs_chan_in_p2p_search,
				p2p_search,
				scan_obj->miracast_enabled);
	}

	for (i = 0; i < req->scan_req.chan_list.num_chan; i++) {
====
	for (i = 0; i < req->scan_req.chan_list.num_chan; i++) {
>>>>
@@@ cmn/umac/scan/core/src/wlan_scan_cache_db.c
<<<<
		if (QDF_GET_BITS(rnr_bss->bss_params, 2, 2) == 0x1) {
			scm_debug("skip nontx freq %d: " QDF_MAC_ADDR_FMT " short ssid %x",
				  chan_freq,
				  QDF_MAC_ADDR_REF(rnr_bss->bssid.bytes),
				  rnr_bss->short_ssid);
			continue;
		}
====
		if (QDF_GET_BITS(rnr_bss->bss_params, 2, 2) == 0x1)
			continue;
>>>>
