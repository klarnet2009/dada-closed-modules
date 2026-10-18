# g4q 50: target_if_dbr_init_ring (stock): "enter" print at the start and a debug print of dbr_psoc_obj before the ring allocation
@@@ cmn/target_if/direct_buf_rx/src/target_if_direct_buf_rx_main.c
<<<<
	struct direct_buf_rx_ring_cfg *dbr_ring_cfg;
	QDF_STATUS status;

	psoc = wlan_pdev_get_psoc(pdev);

	if (!psoc) {
		direct_buf_rx_err("psoc is null");
		return QDF_STATUS_E_FAILURE;
	}

	dbr_psoc_obj = wlan_objmgr_psoc_get_comp_private_obj(psoc,
				WLAN_TARGET_IF_COMP_DIRECT_BUF_RX);

	if (!dbr_psoc_obj) {
		direct_buf_rx_err("dir buf rx psoc object is null");
		return QDF_STATUS_E_FAILURE;
	}

	if (!dbr_psoc_obj->hal_soc ||
====
	struct direct_buf_rx_ring_cfg *dbr_ring_cfg;
	QDF_STATUS status;

	direct_buf_rx_enter();

	psoc = wlan_pdev_get_psoc(pdev);

	if (!psoc) {
		direct_buf_rx_err("psoc is null");
		return QDF_STATUS_E_FAILURE;
	}

	dbr_psoc_obj = wlan_objmgr_psoc_get_comp_private_obj(psoc,
				WLAN_TARGET_IF_COMP_DIRECT_BUF_RX);

	if (!dbr_psoc_obj) {
		direct_buf_rx_err("dir buf rx psoc object is null");
		return QDF_STATUS_E_FAILURE;
	}

	if (!dbr_psoc_obj->hal_soc ||
>>>>
<<<<
	if (!mod_param->dbr_buf_pool)
		return QDF_STATUS_E_NOMEM;

	ring_alloc_size = (num_entries * entry_size) + DBR_RING_BASE_ALIGN - 1;
====
	if (!mod_param->dbr_buf_pool)
		return QDF_STATUS_E_NOMEM;

	direct_buf_rx_debug("dbr_psoc_obj %pK", dbr_psoc_obj);

	ring_alloc_size = (num_entries * entry_size) + DBR_RING_BASE_ALIGN - 1;
>>>>
