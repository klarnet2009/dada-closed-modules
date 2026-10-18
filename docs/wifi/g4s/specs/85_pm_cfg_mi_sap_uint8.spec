# g4s r1: stock stores the INI value of mi_sap_only_allow_sta_dfs_indoor_chan as a raw byte (no != 0 normalisation in ucfg_policy_mgr_psoc_open) and normalises when reading: field is uint8_t
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_i.h
<<<<
	bool mi_sap_only_allow_sta_dfs_indoor_chan;
====
	uint8_t mi_sap_only_allow_sta_dfs_indoor_chan;
>>>>
