/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_WIRELESS_CLASS_H__
#define __PLATFORM_WIRELESS_CLASS_H__

#include <linux/types.h>

enum wireless_role {
	WIRELESS_ROLE_MASTER = 0,
};

int platform_class_wireless_set_enable_mode(unsigned int role, bool enable);

#endif
