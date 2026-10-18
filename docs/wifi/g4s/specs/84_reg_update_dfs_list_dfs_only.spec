# g4s r1: stock reg_update_list_for_dfs_channel (inlined in reg_skip_invalid_chan_freq) tests only the DFS flag of the channel: no 5 GHz range check, no indoor test
@@@ cmn/umac/regulatory/core/src/reg_services_common.c
<<<<
	if (!WLAN_REG_IS_5GHZ_CH_FREQ(res_msg[chan_enum].freq))
		return;

	if (!wlan_reg_is_dfs_for_freq(pdev, res_msg[chan_enum].freq) &&
	    !wlan_reg_is_freq_indoor(pdev, res_msg[chan_enum].freq))
		return;
====
	if (!wlan_reg_is_dfs_for_freq(pdev, res_msg[chan_enum].freq))
		return;
>>>>
