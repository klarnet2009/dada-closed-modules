/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_CP_CLASS_H__
#define __PLATFORM_CP_CLASS_H__

enum cp_role {
	CP_ROLE_MASTER = 0,
	CP_ROLE_SLAVE = 1,
};

int platform_class_cp_get_tdie(unsigned int role, int *tdie);
int platform_class_cp_get_battery_voltage(unsigned int role, int *vbat);

#endif
