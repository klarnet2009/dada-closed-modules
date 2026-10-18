# g4s r1: stock scm_ignore_ssid_check_for_hidden_bss (inlined in scm_filter_match) tests the bssid hint only when the hint is set OR the filter key_mgmt has OWE (bit 22, byte +0x2e bit 6); hint tested first
@@@ cmn/umac/scan/core/src/wlan_scan_filter.c
<<<<
	if (is_hidden &&
	    util_is_bssid_match(&filter->bssid_hint, &db_entry->bssid))
		return true;
====
	if (is_hidden &&
	    (!qdf_is_macaddr_zero(&filter->bssid_hint) ||
	     (filter->key_mgmt & (1 << WLAN_CRYPTO_KEY_MGMT_OWE))) &&
	    util_is_bssid_match(&filter->bssid_hint, &db_entry->bssid))
		return true;
>>>>
