# g4q 36: __wlan_hdd_cfg80211_scan: with scan_ext_flag set (command SetScanExtFlag) and latency level 2/3 the host leaves the low latency mode before the scan
# (hdd_scan_event_callback re-enters it afterwards). Order and messages as in the stock.
@@@ core/hdd/src/wlan_hdd_scan.c
<<<<
	hdd_debug("MLD ID: %d", params.mld_id);

	if (cds_is_10_mhz_enabled())
====
	hdd_debug("MLD ID: %d", params.mld_id);

	if (adapter->scan_ext_flag == 1 &&
	    (adapter->latency_level == 2 || adapter->latency_level == 3)) {
		hdd_debug("exit latency mode before active scan");
		status = sme_set_wlm_latency_level(hdd_ctx->mac_handle,
						   adapter->deflink->vdev_id,
						   0, 0, 0);
		if (QDF_IS_STATUS_ERROR(status))
			hdd_debug("set latency level 0 failed when scan");
	}

	if (cds_is_10_mhz_enabled())
>>>>
