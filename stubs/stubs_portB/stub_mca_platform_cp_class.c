// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_platform_cp_class (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/platform/platform_cp_class.h>

int platform_class_cp_get_tdie(unsigned int role, int *tdie) { return 0; }
EXPORT_SYMBOL(platform_class_cp_get_tdie);
int platform_class_cp_get_battery_voltage(unsigned int role, int *vbat) { return 0; }
EXPORT_SYMBOL(platform_class_cp_get_battery_voltage);

int platform_class_cp_register_ops(unsigned int role, struct platform_class_cp_ops *ops, void *data) { return 0; }
EXPORT_SYMBOL(platform_class_cp_register_ops);
