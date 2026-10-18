# g4q 43: stock hdd_sysfs_create_driver_root_obj has no return after a failed p2phc kobject (falls through to create "wlan" with a NULL parent; OOM only). Kept bug-compatible.
@@@ core/hdd/src/wlan_hdd_sysfs.c
<<<<
		hdd_err("could not allocate p2phc kobject");
		kobject_put(driver_kobject);
		driver_kobject = NULL;
		return;
	}
====
		hdd_err("could not allocate p2phc kobject");
		kobject_put(driver_kobject);
		driver_kobject = NULL;
	}
>>>>
