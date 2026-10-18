# g4s r1: stock policy_mgr_check_scc_channel returns right after the pm_ctx check when the HW is not DBS capable (before counting the STAs)
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_core.c
<<<<
	sta_count = policy_mgr_mode_specific_connection_count(psoc, PM_STA_MODE,
							      NULL);

	if (!is_dbs) {
		if (!sta_count)
			return;

		/* Fetch new freq using PCL */
	}
====
	if (!is_dbs)
		return;

	sta_count = policy_mgr_mode_specific_connection_count(psoc, PM_STA_MODE,
							      NULL);
>>>>
