# g4q 41: sme_set_wlm_latency_level: host latency level 5 (Xiaomi, see spec 35) is sent to the firmware as wlm level 3 with flags 0
@@@ core/sme/src/common/sme_api.c
<<<<
	params.wlm_latency_level = latency_level;
	params.wlm_latency_flags =
		mac_ctx->mlme_cfg->wlm_config.latency_flags[latency_level];
	params.vdev_id = session_id;
====
	if (latency_level == 5) {
		params.wlm_latency_level = 3;
		params.wlm_latency_flags = 0;
	} else {
		params.wlm_latency_level = latency_level;
		params.wlm_latency_flags =
		mac_ctx->mlme_cfg->wlm_config.latency_flags[latency_level];
	}
	params.vdev_id = session_id;
>>>>
