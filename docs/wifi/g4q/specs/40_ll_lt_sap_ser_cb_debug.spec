# g4q 40: ll_lt_sap_high_ap_availability_ser_cb starts with a debug print of the reason (stock)
@@@ components/umac/mlme/sap/ll_sap/core/src/wlan_ll_lt_sap_main.c
<<<<
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	if (!cmd || !cmd->umac_cmd) {
		ll_sap_err("NULL ser cmd");
		return QDF_STATUS_E_NULL_VALUE;
	}

	req = cmd->umac_cmd;
	vdev = cmd->vdev;

	switch (reason) {
	case WLAN_SER_CB_ACTIVATE_CMD:
		/* command moved to active list */
		status = ll_lt_sap_high_ap_availability_process(vdev, req);
====
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	ll_sap_debug("reason %d", reason);

	if (!cmd || !cmd->umac_cmd) {
		ll_sap_err("NULL ser cmd");
		return QDF_STATUS_E_NULL_VALUE;
	}

	req = cmd->umac_cmd;
	vdev = cmd->vdev;

	switch (reason) {
	case WLAN_SER_CB_ACTIVATE_CMD:
		/* command moved to active list */
		status = ll_lt_sap_high_ap_availability_process(vdev, req);
>>>>
