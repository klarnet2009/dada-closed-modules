
/* g4k: wlan_tdls_is_key_install_allowed (newer CAF code, present in the OS4 stock) */
bool wlan_tdls_is_key_install_allowed(struct wlan_objmgr_vdev *vdev, struct qdf_mac_addr *mac_addr)
{
	struct tdls_vdev_priv_obj *vdev_obj;
	struct tdls_peer *curr_peer;

	vdev_obj = wlan_vdev_get_tdls_vdev_obj(vdev);
	if (!vdev_obj) {
		tdls_err("vdev_obj: %pK is null", vdev_obj);
		return false;
	}

	curr_peer = tdls_find_peer(vdev_obj, mac_addr->bytes);
	if (!curr_peer) {
		tdls_err("tdls peer is null");
		return false;
	}

	if (curr_peer->valid_entry &&
	    (curr_peer->link_status == TDLS_LINK_CONNECTING || curr_peer->link_status == TDLS_LINK_CONNECTED))
		return true;

	return false;
}
