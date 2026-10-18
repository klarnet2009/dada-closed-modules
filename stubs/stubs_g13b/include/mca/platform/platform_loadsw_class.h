/* SPDX-License-Identifier: GPL-2.0 */
/* platform_loadsw_class.h - reconstructed API of mca_platform_loadsw_class.ko (ops layout from the stock BTF) */
#ifndef __PLATFORM_LOADSW_CLASS_H__
#define __PLATFORM_LOADSW_CLASS_H__

#include <linux/types.h>

enum loadsw_role {
	LOADSW_ROLE_MASTER = 0,
	LOADSW_ROLE_MAIN = 0,	/* alias used by the first reconstruction of the consumers */
	LOADSW_ROLE_SLAVE = 1,
	LOADSW_ROLE_MAX,
};

struct platform_class_loadsw_ops {
	int (*loadsw_get_present)(bool *present, void *data);
	int (*loadsw_get_ibat_limit)(int *ibat_limit, void *data);
	int (*loadsw_set_ibat_limit)(int val, void *data);
	int (*loadsw_set_lowpower_mode)(bool mode, void *data);
	int (*loadsw_get_lowpower_mode)(bool *mode, void *data);
};

int platform_class_loadsw_register_ops(unsigned int role, struct platform_class_loadsw_ops *ops, void *data);
int platform_class_loadsw_get_present(unsigned int role, bool *present);
int platform_class_loadsw_get_ibat_limit(unsigned int role, int *ibat_limit);
int platform_class_loadsw_set_ibat_limit(unsigned int role, int val);
int platform_class_loadsw_set_lowpower_mode(unsigned int role, bool mode);
int platform_class_loadsw_get_lowpower_mode(unsigned int role, bool *mode);

#endif
