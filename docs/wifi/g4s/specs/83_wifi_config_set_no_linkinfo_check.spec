# g4s r1: stock __wlan_hdd_cfg80211_wifi_configuration_set has no NULL test of the link_info found by hdd_get_link_info_by_link_id (bug-compatible, a NULL link_info is passed on)
@@@ core/hdd/src/wlan_hdd_cfg80211.c
<<<<
		link_info = hdd_get_link_info_by_link_id(adapter, link_id);
		if (!link_info) {
			hdd_err("invalid link_info");
			return -EINVAL;
		}
	}

	ret = hdd_set_independent_configuration(link_info, tb);
====
		link_info = hdd_get_link_info_by_link_id(adapter, link_id);
	}

	ret = hdd_set_independent_configuration(link_info, tb);
>>>>
