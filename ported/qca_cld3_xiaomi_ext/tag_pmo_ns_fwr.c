QDF_STATUS pmo_core_enable_ns_offload_in_fwr(struct wlan_objmgr_vdev *vdev,
		enum pmo_offload_trigger trigger)
{
	QDF_STATUS status;
	struct pmo_vdev_priv_obj *vdev_ctx;
	uint8_t vdev_id;

	if (!vdev) {
		pmo_err("vdev is NULL");
		status = QDF_STATUS_E_INVAL;
		goto out;
	}

	status = pmo_vdev_get_ref(vdev);
	if (status != QDF_STATUS_SUCCESS)
		goto out;

	vdev_ctx = pmo_vdev_get_priv(vdev);

	qdf_spin_lock_bh(&vdev_ctx->pmo_vdev_lock);
	if (vdev_ctx->vdev_ns_req.num_ns_offload_count == 0) {
		qdf_spin_unlock_bh(&vdev_ctx->pmo_vdev_lock);
		pmo_debug("skip ns offload enable as ns count is 0");
		status = QDF_STATUS_E_INVAL;
		goto dec_ref;
	}
	qdf_spin_unlock_bh(&vdev_ctx->pmo_vdev_lock);

	vdev_id = pmo_vdev_get_id(vdev);
	pmo_debug("Enable ns offload in fwr vdev id: %d vdev: %pK trigger: %d",
		vdev_id, vdev, trigger);
	status = pmo_core_do_enable_ns_offload(vdev, vdev_id, trigger);
dec_ref:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_PMO_ID);
out:

	return status;
}

QDF_STATUS pmo_core_disable_ns_offload_in_fwr(struct wlan_objmgr_vdev *vdev,
		enum pmo_offload_trigger trigger)
{
	QDF_STATUS status;
	uint8_t vdev_id;
	struct pmo_vdev_priv_obj *vdev_ctx;

	pmo_enter();
	if (!vdev) {
		pmo_err("vdev is NULL");
		status = QDF_STATUS_E_INVAL;
		goto out;
	}

	status = pmo_vdev_get_ref(vdev);
	if (status != QDF_STATUS_SUCCESS)
		goto out;

	vdev_ctx = pmo_vdev_get_priv(vdev);
	vdev_id = pmo_vdev_get_id(vdev);
	pmo_debug("disable ns offload in fwr vdev id: %d vdev: %pK trigger: %d",
		  vdev_id, vdev, trigger);

	status = pmo_core_do_disable_ns_offload(vdev, vdev_id, trigger);
	wlan_objmgr_vdev_release_ref(vdev, WLAN_PMO_ID);
out:
	pmo_exit();

	return status;
}

