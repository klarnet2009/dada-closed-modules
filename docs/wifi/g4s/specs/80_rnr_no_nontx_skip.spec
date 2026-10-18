# g4s r1: stock scm_add_rnr_channel_db has no "skip non Tx MBSSID profile" test (bss_params bits 2..3) before the list-size check
@@@ cmn/umac/scan/core/src/wlan_scan_cache_db.c
<<<<
		/* Skip non Tx MBSSID profile */
		if (QDF_GET_BITS(rnr_bss->bss_params, 2, 2) == 0x1)
			continue;

		/* Don't add RNR entry if list is full */
====
		/* Don't add RNR entry if list is full */
>>>>
