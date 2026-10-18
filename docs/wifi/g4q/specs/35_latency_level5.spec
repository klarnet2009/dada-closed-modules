# g4q 35: hdd_config_latency_level: the stock accepts latency level 5 (not in the CAF vendor enum) and sends it straight to the firmware (wlm level 5, no host bookkeeping)
@@@ core/hdd/src/wlan_hdd_cfg80211.c
<<<<
	case QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_ULTRALOW:
		/* valid values */
		break;
	default:
		hdd_err("Invalid value %u", latency_level);
		return -EINVAL;
	}

	host_latency_level = latency_level - 1;
====
	case QCA_WLAN_VENDOR_ATTR_CONFIG_LATENCY_LEVEL_ULTRALOW:
	case 5: /* Xiaomi extension */
		/* valid values */
		break;
	default:
		hdd_err("Invalid value %u", latency_level);
		return -EINVAL;
	}

	if (latency_level == 5) {
		status = sme_set_wlm_latency_level(hdd_ctx->mac_handle,
						   link_info->vdev_id, 5, 0,
						   false);
		if (QDF_IS_STATUS_ERROR(status)) {
			hdd_err("Set latency level failed, %u", status);
			goto error;
		}
		return 0;
	}

	host_latency_level = latency_level - 1;
>>>>
