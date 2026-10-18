# g4q 47: __wlan_hdd_cfg80211_get_usable_channel prints the channel count before sending the reply (stock)
@@@ core/hdd/src/wlan_hdd_cfg80211.c
<<<<
		hdd_err("get usable channel failed %d", status);
		ret = -EINVAL;
		goto err;
	}

	ret = hdd_send_usable_channel(hdd_ctx, res_msg, count, tb);
====
		hdd_err("get usable channel failed %d", status);
		ret = -EINVAL;
		goto err;
	}

	hdd_debug("usable channel count : %d", count);

	ret = hdd_send_usable_channel(hdd_ctx, res_msg, count, tb);
>>>>
