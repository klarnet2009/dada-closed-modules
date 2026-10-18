
/* g4k: pmo_core_{get,set}_ns_offload_enable_dynamic (newer CAF code, present in the OS4 stock, not in CLO r1-07500) */
bool
pmo_core_get_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev)
{
	QDF_STATUS status;
	struct pmo_vdev_priv_obj *vdev_ctx;
	struct pmo_psoc_priv_obj *pmo_psoc_ctx;
	bool ret = false;

	if (!vdev) {
		pmo_err("vdev is NULL");
		goto out;
	}

	status = pmo_vdev_get_ref(vdev);
	if (status != QDF_STATUS_SUCCESS)
		goto out;

	status = pmo_core_ns_offload_sanity(vdev);
	if (status != QDF_STATUS_SUCCESS)
		goto dec_ref;

	vdev_ctx = pmo_vdev_get_priv(vdev);
	pmo_psoc_ctx = vdev_ctx->pmo_psoc_ctx;

	ret = pmo_psoc_ctx->psoc_cfg.ns_offload_enable_dynamic;
dec_ref:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_PMO_ID);
out:
	return ret;
}

void
pmo_core_set_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev,
				       enum pmo_offload_trigger trigger,
				       bool ns_offload_enable_dyn)
{
	QDF_STATUS status;
	struct pmo_vdev_priv_obj *vdev_ctx;
	struct pmo_psoc_priv_obj *pmo_psoc_ctx;

	if (!vdev) {
		pmo_err("vdev is NULL");
		goto out;
	}

	status = pmo_vdev_get_ref(vdev);
	if (status != QDF_STATUS_SUCCESS)
		goto out;

	vdev_ctx = pmo_vdev_get_priv(vdev);
	pmo_psoc_ctx = vdev_ctx->pmo_psoc_ctx;

	status = pmo_core_ns_offload_sanity(vdev);
	if (status != QDF_STATUS_SUCCESS)
		goto dec_ref;

	if (trigger == pmo_ns_offload_dynamic_update) {
		pmo_psoc_ctx->psoc_cfg.ns_offload_enable_dynamic = ns_offload_enable_dyn;
		goto dec_ref;
	}
dec_ref:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_PMO_ID);
out:
	return;
}
