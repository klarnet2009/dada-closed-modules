# g4s r3: stock policy_mgr_allow_4th_new_freq returns true after the NAN clauses when sta == 1 || (sta == 2 && p2p == 2) || p2p == 3 (read from the disassembly at stock +0xd30)
@@@ components/cmn_services/policy_mgr/src/wlan_policy_mgr_get_set_utils.c
<<<<
					 sta, nan, sap, ndi);
			return true;
		}
	}

end:
====
					 sta, nan, sap, ndi);
			return true;
		}

		if (sta == 1 || (sta == 2 && p2p == 2) || p2p == 3)
			return true;
	}

end:
>>>>
