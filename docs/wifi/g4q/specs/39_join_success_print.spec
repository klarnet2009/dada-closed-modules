# g4q 39: lim_check_and_announce_join_success prints the frame type ("Beacon" or "PR") as in the stock
@@@ core/mac/src/pe/lim/lim_assoc_utils.c
<<<<
	pe_debug("Received Beacon/PR with BSSID:"QDF_MAC_ADDR_FMT" pe session %d vdev %d",
		 QDF_MAC_ADDR_REF(session_entry->bssId),
====
	pe_debug("Received %s with BSSID:"QDF_MAC_ADDR_FMT" pe session %d vdev %d",
		 header->fc.subType == SIR_MAC_MGMT_BEACON ? "Beacon" : "PR",
		 QDF_MAC_ADDR_REF(session_entry->bssId),
>>>>
