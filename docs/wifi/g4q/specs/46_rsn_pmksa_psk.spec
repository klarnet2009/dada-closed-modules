# g4q 46: lim_fill_rsn_ie (stock): with a PSK based AKM the PMKID of the found PMKSA is cleared ("WPA2 does not support PMKID") so that it is not put into the RSNE
@@@ core/mac/src/pe/lim/lim_process_sme_req_messages.c
<<<<
	pmksa_peer = wlan_crypto_get_peer_pmksa(session->vdev, &pmksa);
	if (pmksa_peer)
		pe_debug("PMKSA found");

	lim_update_connect_rsn_ie(session, rsn_ie, pmksa_peer);
====
	pmksa_peer = wlan_crypto_get_peer_pmksa(session->vdev, &pmksa);
	if (pmksa_peer)
		pe_debug("PMKSA found");

	akm = wlan_crypto_get_param(session->vdev, WLAN_CRYPTO_PARAM_KEY_MGMT);
	if (pmksa_peer &&
	    (akm & (BIT(WLAN_CRYPTO_KEY_MGMT_PSK) |
		    BIT(WLAN_CRYPTO_KEY_MGMT_FT_PSK) |
		    BIT(WLAN_CRYPTO_KEY_MGMT_PSK_SHA256) |
		    BIT(WLAN_CRYPTO_KEY_MGMT_FT_PSK_SHA384) |
		    BIT(WLAN_CRYPTO_KEY_MGMT_PSK_SHA384)))) {
		pe_debug("vdev:%d WPA2 does not support PMKID",
			 session->vdev_id);
		qdf_mem_zero(pmksa_peer->pmkid, PMKID_LEN);
	}

	lim_update_connect_rsn_ie(session, rsn_ie, pmksa_peer);
>>>>
<<<<
	struct wlan_crypto_pmksa pmksa, *pmksa_peer;
	struct bss_description *bss_desc;

	rsn_ie = qdf_mem_malloc(WLAN_MAX_IE_LEN + 2);
====
	struct wlan_crypto_pmksa pmksa, *pmksa_peer;
	struct bss_description *bss_desc;
	int32_t akm;

	rsn_ie = qdf_mem_malloc(WLAN_MAX_IE_LEN + 2);
>>>>
