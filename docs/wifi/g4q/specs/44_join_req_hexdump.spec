# g4q 44: cm_csr_handle_join_req: when the bss descriptor cannot be built, the stock hex-dumps the raw frame of the scan entry before failing
@@@ core/sme/src/csr/csr_api_roam.c
<<<<
	status = wlan_fill_bss_desc_from_scan_entry(mac_ctx, bss_desc,
						    join_req->entry);
	if (QDF_IS_STATUS_ERROR(status)) {
		qdf_mem_free(bss_desc);
		return QDF_STATUS_E_FAILURE;
	}

	status = wlan_get_parsed_bss_description_ies(mac_ctx, bss_desc,
						     &ie_struct);
	if (QDF_IS_STATUS_ERROR(status)) {
		sme_err("IE parsing failed vdev id %d", vdev_id);
		qdf_mem_free(bss_desc);
		return QDF_STATUS_E_FAILURE;
	}

	if (reassoc) {
		csr_update_tspec_info(mac_ctx, vdev, ie_struct);
====
	status = wlan_fill_bss_desc_from_scan_entry(mac_ctx, bss_desc,
						    join_req->entry);
	if (QDF_IS_STATUS_ERROR(status)) {
		mgmt_txrx_frame_hex_dump(join_req->entry->raw_frame.ptr,
					 join_req->entry->raw_frame.len, false);
		qdf_mem_free(bss_desc);
		return QDF_STATUS_E_FAILURE;
	}

	status = wlan_get_parsed_bss_description_ies(mac_ctx, bss_desc,
						     &ie_struct);
	if (QDF_IS_STATUS_ERROR(status)) {
		sme_err("IE parsing failed vdev id %d", vdev_id);
		qdf_mem_free(bss_desc);
		return QDF_STATUS_E_FAILURE;
	}

	if (reassoc) {
		csr_update_tspec_info(mac_ctx, vdev, ie_struct);
>>>>
