# g4q 32: Xiaomi block "Disabling HT40 for vdev %d for IoT AP" (stock lim_check_oui_and_update_session tail + lim_fill_pe_session).
# The stock keeps the flag at byte 0xd1c of struct join_req = inside addIEAssoc.addIEdata (2312 + 2 + 1042); read after handle_ht_capabilityand_ht_info.
# Action OUI id 23 = ACTION_OUI_LIMIT_BW (same id as lim_limit_bw_for_iot_ap). dot11mode|2 == 7 means 11N (5) or 11N_ONLY (7).
@@@ core/mac/src/pe/lim/lim_process_sme_req_messages.c
<<<<
static void lim_check_oui_and_update_session(struct mac_context *mac_ctx,
					     struct pe_session *session,
					     tDot11fBeaconIEs *ie_struct)
{
====
/* stock: flag byte lives at join_req + 0xd1c (addIEAssoc.addIEdata[1042]) */
#define LIM_JOIN_REQ_HT40_DISABLED(jr) ((jr)->addIEAssoc.addIEdata[1042])

static void lim_check_oui_and_update_session(struct mac_context *mac_ctx,
					     struct pe_session *session,
					     tDot11fBeaconIEs *ie_struct)
{
>>>>
<<<<
	if (is_vendor_ap_present)
		lim_update_he_caps_htc(session, !is_vendor_ap_present);
}

static enum mlme_dot11_mode
lim_get_user_dot11_mode(struct wlan_objmgr_vdev *vdev)
====
	if (is_vendor_ap_present)
		lim_update_he_caps_htc(session, !is_vendor_ap_present);

	LIM_JOIN_REQ_HT40_DISABLED(session->lim_join_req) = false;
	if (wlan_reg_is_24ghz_ch_freq(bss_desc->chan_freq) &&
	    wlan_action_oui_search(mac_ctx->psoc, &vendor_ap_search_attr,
				   ACTION_OUI_LIMIT_BW) &&
	    (session->dot11mode == MLME_DOT11_MODE_11N ||
	     session->dot11mode == MLME_DOT11_MODE_11N_ONLY)) {
		pe_debug("Disabling HT40 for vdev %d for IoT AP " QDF_MAC_ADDR_FMT,
			 session->vdev_id,
			 QDF_MAC_ADDR_REF(vendor_ap_search_attr.mac_addr));
		LIM_JOIN_REQ_HT40_DISABLED(session->lim_join_req) = true;
	}
}

static enum mlme_dot11_mode
lim_get_user_dot11_mode(struct wlan_objmgr_vdev *vdev)
>>>>
<<<<
	handle_ht_capabilityand_ht_info(mac_ctx, session);

	session->htSupportedChannelWidthSet = cb_mode ? 1 : 0;
====
	handle_ht_capabilityand_ht_info(mac_ctx, session);

	if (LIM_JOIN_REQ_HT40_DISABLED(session->lim_join_req))
		cb_mode = PHY_SINGLE_CHANNEL_CENTERED;
	session->htSupportedChannelWidthSet = cb_mode ? 1 : 0;
>>>>
