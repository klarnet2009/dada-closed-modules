# g4s r1: stock wlan_hdd_sap_p2p_11ac_overrides (5/6 GHz branch) reads the VHT channel width and overrides ch_width_orig only when it is currently <= 40 MHz
@@@ core/hdd/src/wlan_hdd_hostapd.c
<<<<
		if (!WLAN_REG_IS_24GHZ_CH_FREQ(sap_cfg->chan_freq)) {
			status =
			    ucfg_mlme_get_vht_channel_width(hdd_ctx->psoc,
							    &ch_width);
			if (!QDF_IS_STATUS_SUCCESS(status))
				hdd_err("Failed to set channel_width");
			sap_cfg->ch_width_orig = ch_width;
		} else {
====
		if (!WLAN_REG_IS_24GHZ_CH_FREQ(sap_cfg->chan_freq)) {
			if (sap_cfg->ch_width_orig <= CH_WIDTH_40MHZ) {
				status =
				    ucfg_mlme_get_vht_channel_width(
							hdd_ctx->psoc,
							&ch_width);
				if (!QDF_IS_STATUS_SUCCESS(status))
					hdd_err("Failed to set channel_width");
				sap_cfg->ch_width_orig = ch_width;
			}
		} else {
>>>>
