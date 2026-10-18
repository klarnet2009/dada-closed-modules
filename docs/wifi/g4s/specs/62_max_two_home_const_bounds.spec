# g4s r1: stock policy_mgr_if_max_two_home_channel_used compares ALL pairs of the 5 freq slots for case 3 and 4 (fully unrolled, loop bounds = array size, not mac_freq_num)
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_get_set_utils.c
<<<<
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
====
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
>>>>
