
/* g4k: wma_is_both_psd_eirp_support_present_for_sp (newer CAF code, present in the OS4 stock) */
bool is_both_psd_eirp_support_present_for_sp(wmi_unified_t wmi_handle, struct reg_tpc_power_info *param);

void wma_is_both_psd_eirp_support_present_for_sp(struct reg_tpc_power_info *param, bool *value)
{
	tp_wma_handle wma_handle = cds_get_context(QDF_MODULE_ID_WMA);

	if (!wma_handle) {
		*value = false;
		return;
	}

	*value = is_both_psd_eirp_support_present_for_sp(wma_handle->wmi_handle, param);
}
