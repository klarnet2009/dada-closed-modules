# g4q 54: target_if_dbr_deinit_ring as in the stock (older CAF): "dbr_psoc_obj %pK" print, unconditional hal_srng_cleanup (no cds_is_target_ready check / SRNG-is-NULL message)
@@@ cmn/target_if/direct_buf_rx/src/target_if_direct_buf_rx_main.c
<<<<
		direct_buf_rx_err("dir buf rx psoc object is null");
		return QDF_STATUS_E_FAILURE;
	}

	dbr_ring_cfg = mod_param->dbr_ring_cfg;
	if (dbr_ring_cfg) {
		target_if_dbr_empty_ring(pdev, dbr_psoc_obj, mod_param);

		if (dbr_ring_cfg->srng && cds_is_target_ready())
			hal_srng_cleanup(dbr_psoc_obj->hal_soc,
					 dbr_ring_cfg->srng, 0);
		else
			direct_buf_rx_err("DBR ring config SRNG is NULL tgt_ready:%d",
					  cds_is_target_ready());

		qdf_mem_free_consistent(dbr_psoc_obj->osdev,
====
		direct_buf_rx_err("dir buf rx psoc object is null");
		return QDF_STATUS_E_FAILURE;
	}

	direct_buf_rx_debug("dbr_psoc_obj %pK", dbr_psoc_obj);

	dbr_ring_cfg = mod_param->dbr_ring_cfg;
	if (dbr_ring_cfg) {
		target_if_dbr_empty_ring(pdev, dbr_psoc_obj, mod_param);

		hal_srng_cleanup(dbr_psoc_obj->hal_soc, dbr_ring_cfg->srng, 0);

		qdf_mem_free_consistent(dbr_psoc_obj->osdev,
>>>>
