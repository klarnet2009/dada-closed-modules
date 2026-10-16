/* SPDX-License-Identifier: GPL-2.0 */
/* platform_cp_class.h - reconstructed API of mca_platform_cp_class.ko (ops layout from the stock BTF, prototypes CRC-checked against the stock exporter) */
#ifndef __PLATFORM_CP_CLASS_H__
#define __PLATFORM_CP_CLASS_H__

#include <linux/types.h>

enum cp_role {
	CP_ROLE_MASTER = 0,
	CP_ROLE_SLAVE = 1,
	CP_ROLE_THIRD = 2,
	CP_ROLE_MAX,
};

struct platform_class_cp_ops {
	int (*cp_set_enable)(bool, void *data);
	int (*cp_get_enabled)(bool *, void *data);
	int (*cp_set_present)(bool, void *data);
	int (*cp_get_present)(bool *, void *data);
	int (*cp_get_vbus_present)(bool *, void *data);
	int (*cp_get_battery_voltage)(u32 *, void *data);
	int (*cp_get_battery_current)(u32 *, void *data);
	int (*cp_get_battery_temperature)(u32 *, void *data);
	int (*cp_get_battery_present)(bool *, void *data);
	int (*cp_get_bus_voltage)(u32 *, void *data);
	int (*cp_get_bus_current)(u32 *, void *data);
	int (*cp_get_bus_temperature)(int *, void *data);
	int (*cp_get_die_temperature)(int *, void *data);
	int (*cp_get_alarm_status)(int *, void *data);
	int (*cp_get_fault_status)(int *, void *data);
	int (*cp_get_bus_error_status)(int *, void *data);
	int (*cp_get_reg_status)(int *, void *data);
	int (*cp_enable_wpcgate)(bool, void *data);
	int (*cp_enable_ovpgate)(bool, void *data);
	int (*cp_enable_ovpgate_with_check)(int, bool, void *data);
	int (*cp_get_ovpgate_status)(bool *, void *data);
	int (*cp_get_usb_voltage)(u32 *, void *data);
	int (*cp_set_busovp)(int, void *data);
	int (*cp_set_mode)(int, void *data);
	int (*cp_get_mode)(int *, void *data);
	int (*cp_device_init)(int, void *data);
	int (*cp_enable_adc)(bool, void *data);
	int (*cp_get_bypass_support)(bool *, void *data);
	int (*cp_dump_register)(void *data);
	int (*cp_get_chip_vendor)(int *, void *data);
	int (*cp_enable_acdrv_manual)(bool, void *data);
	int (*cp_set_adjustadble_timeout)(int, void *data);
	int (*cp_get_int_stat)(int, bool *, void *data);
	int (*cp_get_errorhl_stat)(int *, void *data);
	int (*cp_enable_busucp)(bool, void *data);
	int (*cp_set_fsw)(int, void *data);
	int (*cp_set_default_fsw)(void *data);
	int (*cp_get_fsw)(int *, void *data);
	int (*cp_get_fsw_step)(int *, void *data);
	int (*cp_get_tdie)(int *, void *data);
	int (*cp_set_qb)(bool, void *data);
	int (*cp_set_pmid2outuvp_th)(int, void *data);
	int (*cp_set_rcp)(bool, void *data);
	int (*cp_set_revchg)(bool, void *data);
	int (*cp_get_probe_status)(void *data);
	int (*cp_set_pmid2out_uvp_dis)(bool, void *data);
	int (*cp_get_pmid2out_uvp_dis)(bool, void *data);
	int (*cp_set_manual_revchg_mode)(bool, void *data);
	int (*cp_set_cp_reverse_mode)(bool, void *data);
};

int platform_class_cp_register_ops(unsigned int role, struct platform_class_cp_ops *ops, void *data);
int platform_class_cp_set_charging_enable(unsigned int role, bool en);
int platform_class_cp_get_charging_enabled(unsigned int role, bool *en);
int platform_class_cp_set_present(unsigned int role, bool present);
int platform_class_cp_get_vbus_present(unsigned int role, bool *present);
int platform_class_cp_get_present(unsigned int role, bool *present);
int platform_class_cp_get_battery_present(unsigned int role, bool *present);
int platform_class_cp_get_battery_voltage(unsigned int role, int *vbat);
int platform_class_cp_get_battery_current(unsigned int role, int *ibat);
int platform_class_cp_get_battery_temperature(unsigned int role, int *tbat);
int platform_class_cp_get_bus_voltage(unsigned int role, int *vbus);
int platform_class_cp_get_bus_current(unsigned int role, int *ibus);
int platform_class_cp_get_bus_temperature(unsigned int role, int *tbus);
int platform_class_cp_get_die_temperature(unsigned int role, int *tdie);
int platform_class_cp_get_alarm_status(unsigned int role, int *alarm_status);
int platform_class_cp_get_fault_status(unsigned int role, int *fault_status);
int platform_class_cp_get_bus_error_status(unsigned int role, int *error_status);
int platform_class_cp_get_reg_status(unsigned int role, int *reg_status);
int platform_class_cp_set_mode(unsigned int role, int mode);
int platform_class_cp_get_mode(unsigned int role, int *mode);
int platform_class_cp_device_init(unsigned int role, int value);
int platform_class_cp_enable_adc(unsigned int role, bool en);
int platform_class_cp_get_bypass_support(unsigned int role, bool *status);
int platform_class_cp_dump_register(unsigned int role);
int platform_class_cp_get_chip_vendor(unsigned int role, int *chip_vendor);
int platform_class_cp_get_int_stat(unsigned int role, int channel, bool *result);
int platform_class_cp_enable_acdrv_manual(unsigned int role, bool en);
int platform_class_cp_set_adjustadble_timeout(unsigned int role, int value);
int platform_class_cp_enable_wpcgate(unsigned int role, bool en);
int platform_class_cp_enable_ovpgate(unsigned int role, bool en);
int platform_class_cp_enable_ovpgate_with_check(unsigned int role, int type_temp, bool en);
int platform_class_cp_get_ovpgate_status(unsigned int role, bool *en);
int platform_class_cp_get_probe_ok(unsigned int role);
int platform_class_cp_get_usb_voltage(unsigned int role, int *vusb);
int platform_class_cp_get_ibus_delta(int *val);
int platform_class_cp_get_ibus_total(int *val);
int platform_class_cp_get_errorhl_stat(unsigned int role, int *stat);
int platform_class_cp_enable_busucp(unsigned int role, bool en);
int platform_class_cp_set_fsw(unsigned int role, int fsw);
int platform_class_cp_set_default_fsw(unsigned int role);
int platform_class_cp_get_fsw(unsigned int role, int *fsw);
int platform_class_cp_get_fsw_step(unsigned int role, int *fsw_step);
int platform_class_cp_get_tdie(unsigned int role, int *tdie);
int platform_class_cp_set_qb(unsigned int role, bool en);
int platform_class_cp_set_rcp(unsigned int role, bool en);
int platform_class_cp_set_pmid2outuvp_th(unsigned int role, int value);
int platform_class_cp_set_revchg(unsigned int role, bool en);
int platform_class_cp_set_busovp(unsigned int role, int value);
int platform_class_cp_set_pmid2out_uvp_dis(unsigned int role, bool en);
int platform_class_cp_get_pmid2out_uvp_dis(unsigned int role, bool en);
int platform_class_cp_set_manual_revchg_mode(unsigned int role, bool en);
int platform_class_cp_set_cp_reverse_mode(unsigned int role, bool en);

#endif
