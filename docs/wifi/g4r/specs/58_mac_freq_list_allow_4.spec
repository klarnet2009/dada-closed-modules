# g4r 58: policy_mgr_mac_freq_list_allow as in the stock (older CAF): no force_scc (the stock allow_4th_new_freq has no policy_mgr_is_3vifs_mcc_to_scc_enabled
# read), and case 4 (3 existing connections + the new one in the same mac): SCC test on freq[0..2] (as the stock: the 4th frequency is NOT compared), allowed
# combinations 3 AP + 1 STA, 4 AP, 2 AP + 2 STA, 1 AP + 3 STA (sta = STA or P2P client). Decoded from the unrolled stock code at +0x430..+0x57c.
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_get_set_utils.c
<<<<
		uint8_t mac_mode_list[MAX_NUMBER_OF_CONC_CONNECTIONS],
		uint8_t mac_freq_num, bool force_scc)
{
	uint8_t sta = 0, ap = 0, i;

	switch (mac_freq_num) {
	case 1:
	case 2:
		return true;
	case 3:
		/* If 3 vifs are active in same mac, target only support:
		 * 3 vifs are in SCC and 3 vifs are :
		 * 1 STA + 2 APs, or 3 APs
		 */
		if (!force_scc && (mac_freq_list[0] != mac_freq_list[1] ||
		    mac_freq_list[0] != mac_freq_list[2]))
			return false;
		for (i = 0; i < mac_freq_num; i++) {
			if (mac_mode_list[i] == PM_STA_MODE ||
			    mac_mode_list[i] == PM_P2P_CLIENT_MODE)
				sta++;
			else
				ap++;
		}

		if (sta == 1 && ap == 2)
			return true;
		if (ap == 3)
			return true;
		return false;
	default:
		return false;
	}
}
====
		uint8_t mac_mode_list[MAX_NUMBER_OF_CONC_CONNECTIONS],
		uint8_t mac_freq_num)
{
	uint8_t sta = 0, ap = 0, i;

	switch (mac_freq_num) {
	case 1:
	case 2:
		return true;
	case 3:
		/* If 3 vifs are active in same mac, target only support:
		 * 3 vifs are in SCC and 3 vifs are :
		 * 1 STA + 2 APs, or 3 APs
		 */
		if (mac_freq_list[0] != mac_freq_list[1] ||
		    mac_freq_list[0] != mac_freq_list[2])
			return false;
		for (i = 0; i < mac_freq_num; i++) {
			if (mac_mode_list[i] == PM_STA_MODE ||
			    mac_mode_list[i] == PM_P2P_CLIENT_MODE)
				sta++;
			else
				ap++;
		}

		if (sta == 1 && ap == 2)
			return true;
		if (ap == 3)
			return true;
		return false;
	case 4:
		/* stock: only freq[0..2] are compared */
		if (mac_freq_list[0] != mac_freq_list[1] ||
		    mac_freq_list[0] != mac_freq_list[2])
			return false;
		for (i = 0; i < mac_freq_num; i++) {
			if (mac_mode_list[i] == PM_STA_MODE ||
			    mac_mode_list[i] == PM_P2P_CLIENT_MODE)
				sta++;
			else
				ap++;
		}

		if (sta == 1 && ap == 3)
			return true;
		if (ap == 4)
			return true;
		if (sta == 2 && ap == 2)
			return true;
		if (sta == 3 && ap == 1)
			return true;
		return false;
	default:
		return false;
	}
}
>>>>
<<<<
	bool force_scc = policy_mgr_is_3vifs_mcc_to_scc_enabled(psoc);
	qdf_freq_t freq[MAX_NUMBER_OF_CONC_CONNECTIONS] = {0};
	uint8_t max_freq_num = 0;
====
	qdf_freq_t freq[MAX_NUMBER_OF_CONC_CONNECTIONS] = {0};
	uint8_t max_freq_num = 0;
>>>>
<<<<
			if (!policy_mgr_mac_freq_list_allow(
				mac_freq_list, mac_mode_list, mac_freq_num,
				force_scc))
				break;
====
			if (!policy_mgr_mac_freq_list_allow(
				mac_freq_list, mac_mode_list, mac_freq_num))
				break;
>>>>
