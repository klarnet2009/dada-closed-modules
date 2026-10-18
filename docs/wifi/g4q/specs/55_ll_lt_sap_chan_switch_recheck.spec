# g4q 55: __wlan_hdd_cfg80211_ll_lt_sap_high_ap_availability: second chan-switch check after the connection-in-progress block (stock returns -EBUSY there WITHOUT releasing the vdev reference: stock bug, kept bug-compatible)
@@@ core/hdd/src/wlan_hdd_ll_lt_sap.c
<<<<
			status = QDF_STATUS_E_BUSY;
			goto err;
		}
	}

	if (wlan_cfg80211_nla_parse(
====
			status = QDF_STATUS_E_BUSY;
			goto err;
		}
	}

	if (hdd_is_chan_switch_in_progress())
		return -EBUSY;

	if (wlan_cfg80211_nla_parse(
>>>>
