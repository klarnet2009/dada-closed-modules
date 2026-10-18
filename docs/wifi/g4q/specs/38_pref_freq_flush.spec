# g4q 38: __wlan_hdd_cfg80211_get_preferred_freq_list flushes the ipv4/ipv6 notifier works of the adapter right after the enter print (stock: flush_work on netdev priv +0x30 and +0x60)
@@@ core/hdd/src/wlan_hdd_cfg80211.c
<<<<
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(ndev);

	hdd_enter_dev(wdev->netdev);

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return -EINVAL;

	if (wlan_cfg80211_nla_parse(tb,
			       QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST_MAX,
====
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(ndev);

	hdd_enter_dev(wdev->netdev);

	flush_work(&adapter->ipv4_notifier_work);
	flush_work(&adapter->ipv6_notifier_work);

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err("Command not allowed in FTM mode");
		return -EPERM;
	}

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return -EINVAL;

	if (wlan_cfg80211_nla_parse(tb,
			       QCA_WLAN_VENDOR_ATTR_GET_PREFERRED_FREQ_LIST_MAX,
>>>>
