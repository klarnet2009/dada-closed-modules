/* SPDX-License-Identifier: GPL-2.0 */
/* platform_bc12_class.h - BC1.2 detection platform class (provider: mca_platform_bc12_class.ko), reconstructed from the stock BTF */
#ifndef __PLATFORM_BC12_CLASS_H__
#define __PLATFORM_BC12_CLASS_H__

#include <linux/types.h>

enum bc12_role {
	BC12_MAIN_ROLE = 0,
	BC12_AUX_ROLE,
	BC12_MAX_ROLE,
};

struct platform_bc12_class_ops {
	int (*bc12_det_en)(int en, void *data);
	int (*get_charge_type)(int *type, void *data);
};

int platform_bc12_class_ops_register(unsigned int role, struct platform_bc12_class_ops *ops, void *data);

#endif /* __PLATFORM_BC12_CLASS_H__ */
