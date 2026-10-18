
/* g4k: ucfg wrappers of the dynamic ns offload switch */
bool pmo_core_get_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev);
void pmo_core_set_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev, enum pmo_offload_trigger trigger,
					    bool ns_offload_enable_dyn);

bool ucfg_pmo_get_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev)
{
	return pmo_core_get_ns_offload_enable_dynamic(vdev);
}

void ucfg_pmo_set_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev, enum pmo_offload_trigger trigger,
					    bool ns_offload_enable_dyn)
{
	return pmo_core_set_ns_offload_enable_dynamic(vdev, trigger, ns_offload_enable_dyn);
}
