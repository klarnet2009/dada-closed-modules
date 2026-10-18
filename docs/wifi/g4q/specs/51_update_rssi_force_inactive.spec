# g4q 51: wlan_hdd_update_rssi (stock): with no valid RSSI and a force-inactive (MLO standby) vdev the rssi is -96 and snr 0 instead of asking the firmware stats
@@@ core/hdd/src/wlan_hdd_stats.c
<<<<
	if (!link_info->rssi) {
		hdd_get_rssi_snr_by_bssid(mac_handle,
					  sta_ctx->conn_info.bssid.bytes,
					  &link_info->rssi, &snr);
	}
====
	if (!link_info->rssi) {
		if (policy_mgr_vdev_is_force_inactive(
				wlan_vdev_get_psoc(link_info->vdev),
				link_info->vdev_id)) {
			link_info->rssi = -96;
			snr = 0;
		} else {
			hdd_get_rssi_snr_by_bssid(mac_handle,
						  sta_ctx->conn_info.bssid.bytes,
						  &link_info->rssi, &snr);
		}
	}
>>>>
