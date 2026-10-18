# g4q 42: hdd_soc_probe reports the probe result with hdd_start_complete() (stock: errno before osif_psoc_sync_unregister, 0 after trans_stop)
@@@ core/hdd/src/wlan_hdd_driver_ops.c
<<<<
	osif_psoc_sync_trans_stop(psoc_sync);

	return 0;

destroy_sync:
	osif_psoc_sync_unregister(dev);
====
	osif_psoc_sync_trans_stop(psoc_sync);
	hdd_start_complete(0);

	return 0;

destroy_sync:
	hdd_start_complete(errno);
	osif_psoc_sync_unregister(dev);
>>>>
