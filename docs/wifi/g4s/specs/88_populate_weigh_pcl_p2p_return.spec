# g4s r1: stock wlan_hdd_populate_weigh_pcl returns right after the PCL entries for P2P client/GO ("Do not add MCC or unsupported ch in p2p prefer list")
@@@ core/hdd/src/wlan_hdd_cfg80211.c
<<<<
	chan_idx = pcl_len;
	if (!conn_count || policy_mgr_is_hw_dbs_capable(psoc) ||
====
	chan_idx = pcl_len;
	if (intf_mode == PM_P2P_CLIENT_MODE || intf_mode == PM_P2P_GO_MODE) {
		hdd_debug("Do not add MCC or unsupported ch in p2p prefer list");
		return chan_idx;
	}
	if (!conn_count || policy_mgr_is_hw_dbs_capable(psoc) ||
>>>>
