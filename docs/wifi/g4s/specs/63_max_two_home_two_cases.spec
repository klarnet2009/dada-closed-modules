# g4s r2 trial: separate case 3 / case 4 bodies (stock has two unrolled copies)
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_get_set_utils.c
<<<<
	case 3:
	case 4:
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
	case 4:
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
>>>>
