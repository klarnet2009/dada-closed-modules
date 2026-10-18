# g4q 49: if_mgr_validate_candidate: the stock tests wlan_p2p_is_vdev_wfd_r2_mode() a second time inside the AP-assist block (helper inlined; not foldable, external call)
@@@ components/cmn_services/interface_mgr/src/wlan_if_mgr_roam.c
<<<<
		bool is_dfs_owner = false, is_valid_ap_assist = false;

		ie = util_scan_entry_ie_data(candidate_info->scan_entry);
====
		bool is_dfs_owner = false, is_valid_ap_assist = false;

		if (!wlan_p2p_is_vdev_wfd_r2_mode(vdev))
			goto end;

		ie = util_scan_entry_ie_data(candidate_info->scan_entry);
>>>>
