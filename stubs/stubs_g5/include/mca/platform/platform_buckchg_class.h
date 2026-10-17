/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_BUCKCHG_CLASS_H__
#define __PLATFORM_BUCKCHG_CLASS_H__

enum buck_charger_role {
	MAIN_BUCK_CHARGER = 0,
};

int platform_class_buckchg_ops_is_init_ok(unsigned int role);
int platform_class_buckchg_ops_set_boost_voltage(unsigned int role, int voltage);
int platform_class_buckchg_ops_set_boost_enable(unsigned int role, int enable);
int platform_class_buckchg_ops_get_otg_boost_enable_status(unsigned int role, int *status);
int platform_class_buckchg_ops_get_otg_gate_enable_status(unsigned int role, int *status);
int platform_class_buckchg_ops_get_online(unsigned int role, int *online);
int platform_class_buckchg_ops_set_too_hot_limit(unsigned int role, int enable);
int platform_class_buckchg_ops_get_bus_volt(unsigned int role, int *bus_volt);
int platform_class_buckchg_ops_get_bus_curr(unsigned int role, int *bus_curr);
int platform_class_buckchg_ops_get_usb_sns_volt(unsigned int role, int *bus_volt);
int platform_class_buckchg_ops_get_chg_status(unsigned int role, int *chg_status);
int platform_class_buckchg_ops_get_pack_vbat(unsigned int role, int *pvbat);
int platform_class_buckchg_ops_get_pack_ibat(unsigned int role, int *pibat);
int platform_class_buckchg_ops_get_pack_tbat(unsigned int role, int *ptbat);

#endif
