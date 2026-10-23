/* SPDX-License-Identifier: GPL-2.0 */
/* platform_cp_class.h - reconstructed: struct from the stock mca_platform_cp_class.ko BTF (size 392), prototypes from the XRING source. */
#ifndef __PLATFORM_CP_CLASS_H__
#define __PLATFORM_CP_CLASS_H__

#include <linux/types.h>

enum cp_role {
	CP_ROLE_MASTER = 0,
	CP_ROLE_SLAVE = 1,
	CP_ROLE_MAX,
};

enum cp_int_stat {
	VOUT_OK_REV_STAT = 0,
	VOUT_OK_CHG_STAT,
	VOUT_INSERT_STAT,
	VBUS_PRESENT_STAT,
	VWPC_PRESENT_STAT,
	VUSB_PRESENT_STAT,
};

enum cp_pmid_error {
	CP_PMID_ERROR_OK = 0,
	CP_PMID_ERROR_LOW = 1,
	CP_PMID_ERROR_HIGH = 2,
};

struct platform_class_cp_ops {
	int (*cp_set_enable)(bool, void *);
	int (*cp_get_enabled)(bool *, void *);
	int (*cp_set_present)(bool, void *);
	int (*cp_get_present)(bool *, void *);
	int (*cp_get_vbus_present)(bool *, void *);
	int (*cp_get_battery_voltage)(u32 *, void *);
	int (*cp_get_battery_current)(u32 *, void *);
	int (*cp_get_battery_temperature)(u32 *, void *);
	int (*cp_get_battery_present)(bool *, void *);
	int (*cp_get_bus_voltage)(u32 *, void *);
	int (*cp_get_bus_current)(u32 *, void *);
	int (*cp_get_bus_temperature)(int *, void *);
	int (*cp_get_die_temperature)(int *, void *);
	int (*cp_get_alarm_status)(int *, void *);
	int (*cp_get_fault_status)(int *, void *);
	int (*cp_get_bus_error_status)(int *, void *);
	int (*cp_get_reg_status)(int *, void *);
	int (*cp_enable_wpcgate)(bool, void *);
	int (*cp_enable_ovpgate)(bool, void *);
	int (*cp_enable_ovpgate_with_check)(int, bool, void *);
	int (*cp_get_ovpgate_status)(bool *, void *);
	int (*cp_get_usb_voltage)(u32 *, void *);
	int (*cp_set_busovp)(int, void *);
	int (*cp_set_mode)(int, void *);
	int (*cp_get_mode)(int *, void *);
	int (*cp_device_init)(int, void *);
	int (*cp_enable_adc)(bool, void *);
	int (*cp_get_bypass_support)(bool *, void *);
	int (*cp_dump_register)(void *);
	int (*cp_get_chip_vendor)(int *, void *);
	int (*cp_enable_acdrv_manual)(bool, void *);
	int (*cp_set_adjustadble_timeout)(int, void *);
	int (*cp_get_int_stat)(int, bool *, void *);
	int (*cp_get_errorhl_stat)(int *, void *);
	int (*cp_enable_busucp)(bool, void *);
	int (*cp_set_fsw)(int, void *);
	int (*cp_set_default_fsw)(void *);
	int (*cp_get_fsw)(int *, void *);
	int (*cp_get_fsw_step)(int *, void *);
	int (*cp_get_tdie)(int *, void *);
	int (*cp_set_qb)(bool, void *);
	int (*cp_set_pmid2outuvp_th)(int, void *);
	int (*cp_set_rcp)(bool, void *);
	int (*cp_set_revchg)(bool, void *);
	int (*cp_get_probe_status)(void *);
	int (*cp_set_pmid2out_uvp_dis)(bool, void *);
	int (*cp_get_pmid2out_uvp_dis)(bool, void *);
	int (*cp_set_manual_revchg_mode)(bool, void *);
	int (*cp_set_cp_reverse_mode)(bool, void *);
};

int platform_class_cp_register_ops(unsigned int role, struct platform_class_cp_ops *ops, void *data);
int platform_class_cp_get_tdie(unsigned int role, int *tdie);
int platform_class_cp_get_battery_voltage(unsigned int role, int *vbat);

#endif
