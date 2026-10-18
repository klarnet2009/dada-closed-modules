# g4r 56: pe_session byte at +0x300a (stock; between is_oui_auth_assoc_6mbps_2ghz_enable and is_unexpected_peer_error, all later bytes shift by 1 and
# dfs_p2p_info stays at 0x3010) read by populate_dot11f_ht_caps (clears supportedChannelWidthSet) and by lim_update_sta_run_time_ht_switch_chnl_params
# ("Preventing HT40 switch for IoT AP on vdev %d"). NO WRITER exists in the stock (offscan of the whole stock disassembly: only these 3 loads), so the flag is
# always 0 on a real run (field is zero-initialised by pe_create_session's memset): kept as in the stock, bug-compatible.
# Also lim_sta_send_add_bss: older channel-width logic (EHT op info -> pe ch_width; VHT caps present -> pe ch_width; HT supported-width-set -> 40 MHz; else 20)
# and the final width cap after the 5/10 MHz override (EHT 320, VHT/HE 160, else 40 MHz), both read from the disassembly.
@@@ core/mac/src/pe/include/lim_session.h
<<<<
	bool is_oui_auth_assoc_6mbps_2ghz_enable;
	bool is_unexpected_peer_error;
====
	bool is_oui_auth_assoc_6mbps_2ghz_enable;
	/* Xiaomi: stock byte at pe_session+0x300a, read-only (never written in the stock) */
	bool ht40_disabled_for_iot_ap;
	bool is_unexpected_peer_error;
>>>>
@@@ core/mac/src/sys/legacy/src/utils/src/parser_api.c
<<<<
		} else {
			pDot11f->supportedChannelWidthSet =
				pe_session->htSupportedChannelWidthSet;
		}

		pDot11f->advCodingCap = pe_session->ht_config.adv_coding_cap;
====
		} else {
			pDot11f->supportedChannelWidthSet =
				pe_session->htSupportedChannelWidthSet;
		}
		if (pe_session->ht40_disabled_for_iot_ap)
			pDot11f->supportedChannelWidthSet = 0;

		pDot11f->advCodingCap = pe_session->ht_config.adv_coding_cap;
>>>>
@@@ core/mac/src/pe/lim/lim_utils.c
<<<<
		pe_debug("Current channel doesn't match HT info ignore");
		return;
	}

	if (pe_session->htSecondaryChannelOffset !=
====
		pe_debug("Current channel doesn't match HT info ignore");
		return;
	}

	if (pe_session->ht40_disabled_for_iot_ap &&
	    wlan_reg_is_24ghz_ch_freq(pe_session->curr_op_freq)) {
		pe_debug("Preventing HT40 switch for IoT AP on vdev %d",
			 pe_session->vdev_id);
		return;
	}

	if (pe_session->htSecondaryChannelOffset !=
>>>>
@@@ core/mac/src/pe/lim/lim_assoc_utils.c
<<<<
		if ((chan_width_support &&
		     ((pAssocRsp->HTCaps.supportedChannelWidthSet) ||
		      (pBeaconStruct->HTCaps.present &&
		       pBeaconStruct->HTCaps.supportedChannelWidthSet))) ||
		    lim_is_eht_connection_op_info_present(pe_session,
							  pAssocRsp)) {
			pAddBssParams->ch_width =
					pe_session->ch_width;
			pAddBssParams->staContext.ch_width =
						pe_session->ch_width;
		} else {
====
		if (lim_is_eht_connection_op_info_present(pe_session,
							  pAssocRsp) ||
		    (chan_width_support && pAssocRsp->VHTCaps.present)) {
			pAddBssParams->ch_width =
					pe_session->ch_width;
			pAddBssParams->staContext.ch_width =
						pe_session->ch_width;
		} else if (chan_width_support &&
			   ((pAssocRsp->HTCaps.supportedChannelWidthSet) ||
			    (pBeaconStruct->HTCaps.present &&
			     pBeaconStruct->HTCaps.supportedChannelWidthSet))) {
			pAddBssParams->ch_width = CH_WIDTH_40MHZ;
			pAddBssParams->staContext.ch_width = CH_WIDTH_40MHZ;
		} else {
>>>>
<<<<
		pAddBssParams->ch_width = CH_WIDTH_10MHZ;
		pAddBssParams->staContext.ch_width = CH_WIDTH_10MHZ;
	}
	lim_set_sta_ctx_twt(&pAddBssParams->staContext, pe_session);
====
		pAddBssParams->ch_width = CH_WIDTH_10MHZ;
		pAddBssParams->staContext.ch_width = CH_WIDTH_10MHZ;
	}
	{
		enum phy_ch_width max_ch_width;

		if (lim_is_eht_connection_op_info_present(pe_session, pAssocRsp))
			max_ch_width = CH_WIDTH_320MHZ;
		else if ((pe_session->vhtCapability && pAssocRsp->VHTCaps.present) ||
			 (lim_is_session_he_capable(pe_session) &&
			  pAssocRsp->he_cap.present))
			max_ch_width = CH_WIDTH_160MHZ;
		else
			max_ch_width = CH_WIDTH_40MHZ;
		if (pAddBssParams->ch_width > max_ch_width) {
			pAddBssParams->ch_width = max_ch_width;
			pAddBssParams->staContext.ch_width = max_ch_width;
		}
	}
	lim_set_sta_ctx_twt(&pAddBssParams->staContext, pe_session);
>>>>
