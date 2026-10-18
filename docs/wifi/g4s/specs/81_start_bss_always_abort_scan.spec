# g4s r1: stock __wlan_hdd_hostapd_start_bss always aborts the non-SAP scans (no policy_mgr_is_vdev_ll_lt_sap test)
@@@ core/hdd/src/wlan_hdd_hostapd.c
<<<<
	if (!policy_mgr_is_vdev_ll_lt_sap(hdd_ctx->psoc, link_info->vdev_id)) {
		/* Cancel all ongoing/pending no sap scan requests */
		hdd_abort_non_sap_scan_all_adapters(hdd_ctx);
	}
====
	/* Cancel all ongoing/pending no sap scan requests */
	hdd_abort_non_sap_scan_all_adapters(hdd_ctx);
>>>>
