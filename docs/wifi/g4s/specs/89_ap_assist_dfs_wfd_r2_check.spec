# g4s r1: stock hdd_check_ap_assist_dfs_group_start_req first re-checks p2p_is_vdev_wfd_r2_mode(vdev) and returns success when not in WFD R2 mode (before the DFS master test)
@@@ core/hdd/src/wlan_hdd_hostapd.c
<<<<
	psoc = wlan_pdev_get_psoc(pdev);

	ucfg_mlme_get_dfs_master_capability(psoc, &is_dfs_master);
	if (is_dfs_master) {
		hdd_debug_rl("Driver is DFS Master Cap so ignore checks");
====
	psoc = wlan_pdev_get_psoc(pdev);

	if (!ucfg_p2p_is_vdev_wfd_r2_mode(vdev)) {
		status = QDF_STATUS_SUCCESS;
		goto vdev_ref;
	}

	ucfg_mlme_get_dfs_master_capability(psoc, &is_dfs_master);
	if (is_dfs_master) {
		hdd_debug_rl("Driver is DFS Master Cap so ignore checks");
>>>>
