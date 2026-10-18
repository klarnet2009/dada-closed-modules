# g4s r1: stock policy_mgr_dfs_master_cfg_changed clears sta_sap_scc_on_dfs_chnl only for value 2 (FLEX) when the device is not dfs master capable
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_get_set_utils.c
<<<<
	cfg->sta_sap_scc_on_dfs_chnl =
		cfg_get(psoc, CFG_STA_SAP_SCC_ON_DFS_CHAN);
	if (!dfs_master_capable)
		cfg->sta_sap_scc_on_dfs_chnl = 0;
====
	cfg->sta_sap_scc_on_dfs_chnl =
		cfg_get(psoc, CFG_STA_SAP_SCC_ON_DFS_CHAN);
	if (!dfs_master_capable &&
	    cfg->sta_sap_scc_on_dfs_chnl == PM_STA_SAP_ON_DFS_MASTER_MODE_FLEX)
		cfg->sta_sap_scc_on_dfs_chnl = 0;
>>>>
