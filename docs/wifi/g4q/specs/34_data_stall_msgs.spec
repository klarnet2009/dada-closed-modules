# g4q 34: hdd_config_data_stall_param as in the stock: start message (error level), missing attribute -> message + return 0, params print at error level
@@@ core/hdd/src/wlan_hdd_cfg80211.c
<<<<
	if (!tx_no_ack_duration_attr || !tx_no_ack_threshold_attr)
		return 0;

	tx_no_ack_duration = nla_get_u16(tx_no_ack_duration_attr);
	tx_no_ack_threshold = nla_get_u16(tx_no_ack_threshold_attr);

	hdd_debug("tx_no_ack_duration %d, tx_no_ack_threshold %d",
		  tx_no_ack_duration, tx_no_ack_threshold);
====
	hdd_err("Set data stall params from use space start");
	if (!tx_no_ack_duration_attr || !tx_no_ack_threshold_attr) {
		hdd_err("Set data stall params fail");
		return 0;
	}

	tx_no_ack_duration = nla_get_u16(tx_no_ack_duration_attr);
	tx_no_ack_threshold = nla_get_u16(tx_no_ack_threshold_attr);

	hdd_err("tx_no_ack_duration %d, tx_no_ack_threshold %d",
		tx_no_ack_duration, tx_no_ack_threshold);
>>>>
