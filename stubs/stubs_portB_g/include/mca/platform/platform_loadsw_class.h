/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_LOADSW_CLASS_H__
#define __PLATFORM_LOADSW_CLASS_H__

#include <linux/types.h>

enum loadsw_role {
	LOADSW_ROLE_MAIN = 0,
};

int platform_class_loadsw_set_lowpower_mode(unsigned int role, bool mode);

#endif
