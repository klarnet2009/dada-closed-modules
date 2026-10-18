# g4s r1: the stock __scm_handle_bcn_probe has no scm_skip_bcn_ch_mismatch_check_by_oui at all (no 32-byte zeroed attr, no call): drop frames on channel mismatch unconditionally; the unused static function is removed (-Werror=unused-function in the real build)
@@@ cmn/umac/scan/core/src/wlan_scan_cache_db.c
<<<<
		if (scan_obj->drop_bcn_on_chan_mismatch &&
		    scan_entry->channel_mismatch &&
		    !scm_skip_bcn_ch_mismatch_check_by_oui(psoc, scan_entry,
							   bcn)) {
====
		if (scan_obj->drop_bcn_on_chan_mismatch &&
		    scan_entry->channel_mismatch) {
>>>>
<<<<
/**
 * scm_skip_bcn_ch_mismatch_check_by_oui() - check chan mismatch beacon
 * frame drop or not based on OUI config
 * @psoc: psoc ptr
 * @scan_entry: scan entry
 * @bcn: beacon frame info
 *
 * For some IOT APs, they are always switching channel for STA to scan and
 * find it, but not connecting to it. But such behaviour APs, don't drop
 * the beacon if channel mismatching.
 *
 * Return: QDF_STATUS
 */
static bool scm_skip_bcn_ch_mismatch_check_by_oui(
			struct wlan_objmgr_psoc *psoc,
			struct scan_cache_entry *scan_entry,
			struct scan_bcn_probe_event *bcn)
{
	struct action_oui_search_attr attr;

	qdf_mem_zero(&attr, sizeof(attr));
	attr.ie_data = util_scan_entry_ie_data(scan_entry);
	attr.ie_length = util_scan_entry_ie_len(scan_entry);

	return false; /* g4: newer OnePlus cmn feature, not in the Xiaomi release */

	scm_nofl_debug(QDF_MAC_ADDR_FMT ": frame(%d) chan mismatch no-drop by oui, seq %d frame freq %d rx data freq %d RSSI %d",
		       QDF_MAC_ADDR_REF(
		       scan_entry->bssid.bytes),
		       bcn->frm_type,
		       scan_entry->seq_num,
		       scan_entry->channel.chan_freq,
		       bcn->rx_data->chan_freq,
		       scan_entry->rssi_raw);

	return true;
}

====
>>>>
