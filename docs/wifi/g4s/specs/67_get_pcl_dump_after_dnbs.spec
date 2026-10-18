# g4s r1: stock policy_mgr_get_pcl dumps the channel list right after policy_mgr_modify_pcl_based_on_dnbs, before checking its status
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_pcl.c
<<<<
	status = policy_mgr_modify_pcl_based_on_dnbs(psoc, pcl_channels,
						pcl_weight, len);

	if (QDF_IS_STATUS_ERROR(status)) {
		policy_mgr_err("failed to get modified pcl based on DNBS");
		return status;
	}
====
	status = policy_mgr_modify_pcl_based_on_dnbs(psoc, pcl_channels,
						pcl_weight, len);
	policy_mgr_dump_channel_list(*len, pcl_channels, pcl_weight);

	if (QDF_IS_STATUS_ERROR(status)) {
		policy_mgr_err("failed to get modified pcl based on DNBS");
		return status;
	}
>>>>
