# g4s r1: stock __hdd_cm_disconnect_handler_post_user_update does not call hdd_wmm_adapter_clear before hdd_adapter_reset_station_ctx
@@@ core/hdd/src/wlan_hdd_cm_disconnect.c
<<<<
			hdd_wmm_adapter_clear(adapter);
			hdd_adapter_reset_station_ctx(adapter);
====
			hdd_adapter_reset_station_ctx(adapter);
>>>>
