
/* g4k */
bool wlan_tdls_is_key_install_allowed(struct wlan_objmgr_vdev *vdev, struct qdf_mac_addr *mac_addr);

bool ucfg_tdls_is_key_install_allowed(struct wlan_objmgr_vdev *vdev, struct qdf_mac_addr *mac_addr)
{
	return wlan_tdls_is_key_install_allowed(vdev, mac_addr);
}
