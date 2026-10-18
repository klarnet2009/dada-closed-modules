# g4s r1: stock message of sap_check_and_update_vdev_ch_params has no vdev id
@@@ core/sap/src/sap_fsm.c
<<<<
	sap_debug("vdev %d updated BW %d -> %d", sap_ctx->vdev_id,
		  orig_ch_width, sap_ctx->ch_params.ch_width);
====
	sap_debug("updated BW %d -> %d",
		  orig_ch_width, sap_ctx->ch_params.ch_width);
>>>>
