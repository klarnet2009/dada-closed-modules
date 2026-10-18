# g4q 37: wlan_hdd_add_key_mlo_vdev: for a pairwise key of a STA adapter whose peer is a TDLS peer the key install is refused (returns 0)
# unless wlan_tdls_is_key_install_allowed() says yes (the function already exists in our tree, only the caller was missing).
@@@ core/hdd/src/wlan_hdd_cfg80211.c
<<<<
			hdd_err("Posting of link connect request failed");
			return -EINVAL;
		}
	}

	link_vdev = ucfg_tdls_get_tdls_link_vdev(vdev, WLAN_OSIF_TDLS_ID);
====
			hdd_err("Posting of link connect request failed");
			return -EINVAL;
		}
	}

	if (pairwise && adapter->device_mode == QDF_STA_MODE) {
		struct qdf_mac_addr peer_mac = {0};
		struct wlan_objmgr_peer *peer;
		enum wlan_peer_type peer_type;

		qdf_mem_copy(peer_mac.bytes, mac_addr, QDF_MAC_ADDR_SIZE);
		peer = wlan_objmgr_get_peer_by_mac(adapter->hdd_ctx->psoc,
						   peer_mac.bytes, WLAN_OSIF_ID);
		if (peer) {
			peer_type = wlan_peer_get_peer_type(peer);
			wlan_objmgr_peer_release_ref(peer, WLAN_OSIF_ID);
			if (peer_type == WLAN_PEER_TDLS &&
			    !ucfg_tdls_is_key_install_allowed(vdev, &peer_mac)) {
				hdd_debug("TDLS peer's key install disallowed");
				return 0;
			}
		}
	}

	link_vdev = ucfg_tdls_get_tdls_link_vdev(vdev, WLAN_OSIF_TDLS_ID);
>>>>
@@@ components/tdls/dispatcher/inc/wlan_tdls_ucfg_api.h
<<<<
bool ucfg_tdls_is_vdev_allowed_to_tx(struct wlan_objmgr_vdev *vdev);
====
bool ucfg_tdls_is_vdev_allowed_to_tx(struct wlan_objmgr_vdev *vdev);

/**
 * ucfg_tdls_is_key_install_allowed() - check if a key may be installed for a TDLS peer
 * @vdev: vdev object
 * @mac_addr: peer mac address
 *
 * Return: true if the key install is allowed
 */
bool ucfg_tdls_is_key_install_allowed(struct wlan_objmgr_vdev *vdev,
				      struct qdf_mac_addr *mac_addr);
>>>>
