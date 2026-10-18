# g4s r1: stock reg_skip_invalid_chan_freq: with exactly one connection its freq is remembered ("allow sap/go to use freq %u"); an indoor channel is removed for SAP/GO unless it is that freq AND indoor channels are supported
@@@ cmn/umac/regulatory/core/src/reg_services_common.c
<<<<
#include "wlan_mlme_ucfg_api.h"
#include "wlan_nan_api.h"
====
#include "wlan_mlme_ucfg_api.h"
#include "wlan_nan_api.h"
#include "wlan_policy_mgr_api.h"
#include "wlan_policy_mgr_i.h"
>>>>
<<<<
	bool include_indoor_channel, dfs_master_capable;
	uint8_t enable_srd_chan, srd_mask = 0;
	struct wlan_objmgr_psoc *psoc;
	enum reg_6g_ap_type ap_pwr_type;
====
	bool include_indoor_channel, dfs_master_capable;
	uint8_t enable_srd_chan, srd_mask = 0;
	uint32_t conc_freq = 0;
	struct wlan_objmgr_psoc *psoc;
	enum reg_6g_ap_type ap_pwr_type;
>>>>
<<<<
	wlan_reg_get_cur_6g_ap_pwr_type(pdev, &ap_pwr_type);

	if (ap_pwr_type == REG_INDOOR_AP)
====
	if (policy_mgr_get_connection_count(psoc) == 1) {
		conc_freq = pm_conc_connection_list[0].freq;
		reg_debug("allow sap/go to use freq %u", conc_freq);
	}

	wlan_reg_get_cur_6g_ap_pwr_type(pdev, &ap_pwr_type);

	if (ap_pwr_type == REG_INDOOR_AP)
>>>>
<<<<
				if (!reg_is_state_allowed(chan_state) ||
				    (wlan_reg_is_freq_indoor(
					pdev, res_msg[chan_enum].freq) &&
					!include_indoor_channel)) {
====
				if (!reg_is_state_allowed(chan_state) ||
				    (wlan_reg_is_freq_indoor(
					pdev, res_msg[chan_enum].freq) &&
				     (res_msg[chan_enum].freq != conc_freq ||
				      !include_indoor_channel))) {
>>>>
