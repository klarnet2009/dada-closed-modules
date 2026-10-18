# g4r 60 (iteration 1 of 3, codegen hypothesis): the stock policy_mgr_allow_4th_new_freq contains two unrolled copies of policy_mgr_if_max_two_home_channel_used
# (mac_freq_num 3 and 4: pair loops bounded by mac_freq_num, count of "match_freq == 1" sets >= 1). Written as `case 3: case 4:` with loops bounded by mac_freq_num.
# Semantics unchanged for 3 entries (freq[3] is 0 and never matches), adds the 4-entry case that the old code returned false for.
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_get_set_utils.c
<<<<
	case 3:
		for (i = 0; i < MAX_NUMBER_OF_CONC_CONNECTIONS; i++) {
			for (j = i + 1; j < MAX_NUMBER_OF_CONC_CONNECTIONS; j++) {
				if (freq[i] == freq[j] && freq[i] != 0 &&
				    freq[j] != 0) {
					match_freq[i]++;
					freq[i] = 0;
					freq[j] = 0;
				}
			}
		}

		for (i = 0;   i < MAX_NUMBER_OF_CONC_CONNECTIONS; i++) {
			if (match_freq[i] == 1)
				freq_sets++;
		}
		return freq_sets >= 1;
====
	case 3:
	case 4:
		for (i = 0; i < mac_freq_num; i++) {
			for (j = i + 1; j < mac_freq_num; j++) {
				if (freq[i] == freq[j] && freq[i] != 0 &&
				    freq[j] != 0) {
					match_freq[i]++;
					freq[i] = 0;
					freq[j] = 0;
				}
			}
		}

		for (i = 0;   i < mac_freq_num; i++) {
			if (match_freq[i] == 1)
				freq_sets++;
		}
		return freq_sets >= 1;
>>>>
