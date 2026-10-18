# g4r 57: lim_send_assoc_req_mgmt_frame as in the stock: the stripped RSNXE (EID 0xf4) and MSCS ext IE are appended to the frame (public CAF r1-07500 strips them
# and never puts them back: the stock has 8 copies after pack_core, ours 6) and counted in the frame size; copy order in the stock: fils hlp, eht cap,
# rsnx, mscs, rsn_sel, vendor ies, mbo, adaptive 11r, mlo. New 12-argument debug message. (PMF check / aead_encrypt order: same flow as ours, only block placement differs.)
@@@ core/mac/src/pe/lim/lim_send_management_frames.c
<<<<
		vendor_ie_len + mlo_ie_len + fils_hlp_ie_len +
		eht_cap_ie_len + rsn_sel_ie_len;
====
		vendor_ie_len + mlo_ie_len + fils_hlp_ie_len +
		eht_cap_ie_len + rsn_sel_ie_len + rsnx_ie_len +
		mscs_ext_ie_len;
>>>>
<<<<
		payload += eht_cap_ie_len;
	}

	/* Need to put all the vendor specific IE to the nd of frame*/
====
		payload += eht_cap_ie_len;
	}

	if (rsnx_ie && rsnx_ie_len) {
		qdf_mem_copy(frame + sizeof(tSirMacMgmtHdr) + payload,
			     rsnx_ie, rsnx_ie_len);
		payload = payload + rsnx_ie_len;
	}

	if (mscs_ext_ie && mscs_ext_ie_len) {
		qdf_mem_copy(frame + sizeof(tSirMacMgmtHdr) + payload,
			     mscs_ext_ie, mscs_ext_ie_len);
		payload = payload + mscs_ext_ie_len;
	}

	/* Need to put all the vendor specific IE to the nd of frame*/
>>>>
<<<<
	pe_debug("extr_ext_flag %d mbo ie len %d is open auth %d stripped vendor len %d he with tkip %d ht %d vht %d opmode %d vendor vht %d he %d eht %d",
		 extr_ext_flag, mbo_ie_len, is_open_auth, current_len,
		 pe_session->he_with_wep_tkip,
		 frm->HTCaps.present, frm->VHTCaps.present,
		 frm->OperatingMode.present, frm->vendor_vht_ie.present,
		 frm->he_cap.present, frm->eht_cap.present);
====
	pe_debug("Assoc Req IEs: dot11mode %d, extcap %d, open %d, IE len:: mbo %d vendor %d, rsnx %d, mscs %d, rsn_sel %d, ft11r %d, mlo %d, eht %d, fils %d",
		 pe_session->dot11mode, extr_ext_flag, is_open_auth,
		 mbo_ie_len, vendor_ie_len, rsnx_ie_len, mscs_ext_ie_len,
		 rsn_sel_ie_len, adaptive_11r_ie_len, mlo_ie_len,
		 eht_cap_ie_len, fils_hlp_ie_len);
>>>>
