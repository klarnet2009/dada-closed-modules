# g4s r1: stock hdd_wmm_get_user_priority_from_ip_tos: when the packet has no TOS (tos == 0) and skb->priority is 1..7, that priority is the user priority (read from the disassembly of hdd_wmm_select_queue at stock +0x1d4)
@@@ core/hdd/src/wlan_hdd_wmm.c
<<<<
	*user_pri = adapter->dscp_to_up_map[dscp];

#ifdef HDD_WMM_DEBUG
====
	*user_pri = adapter->dscp_to_up_map[dscp];
	if (!tos && skb->priority >= 1 && skb->priority <= 7)
		*user_pri = skb->priority;

#ifdef HDD_WMM_DEBUG
>>>>
