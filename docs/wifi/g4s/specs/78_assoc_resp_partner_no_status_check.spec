# g4s r1: stock sir_convert_assoc_resp_frame2_struct validates/copies the ML partner info without testing status_code == STATUS_SUCCESS
@@@ core/mac/src/sys/legacy/src/utils/src/parser_api.c
<<<<
	if (p_assoc_rsp->status_code == STATUS_SUCCESS &&
	    !wlan_vdev_mlme_is_mlo_link_vdev(session_entry->vdev) &&
====
	if (!wlan_vdev_mlme_is_mlo_link_vdev(session_entry->vdev) &&
>>>>
