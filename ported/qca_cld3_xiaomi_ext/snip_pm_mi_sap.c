
/* g4l: policy_mgr_is_sap_only_allow_sta_dfs_indoor_chan (Xiaomi INI mi_sap_only_allow_sta_dfs_indoor_chan; present in the OS4 stock) */
bool policy_mgr_is_sap_only_allow_sta_dfs_indoor_chan(struct wlan_objmgr_psoc *psoc)
{
	struct policy_mgr_psoc_priv_obj *pm_ctx;

	pm_ctx = policy_mgr_get_context(psoc);
	if (!pm_ctx) {
		policy_mgr_err("Invalid Context");
		return true;
	}

	return pm_ctx->cfg.mi_sap_only_allow_sta_dfs_indoor_chan;
}
