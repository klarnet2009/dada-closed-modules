// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_platform_cp_class.ko exports. */
#include <linux/module.h>
#include <mca/platform/platform_cp_class.h>

int platform_class_cp_get_ovpgate_status(unsigned int role, bool *en) { return 0; }
EXPORT_SYMBOL(platform_class_cp_get_ovpgate_status);
int platform_class_cp_enable_ovpgate(unsigned int role, bool en) { return 0; }
EXPORT_SYMBOL(platform_class_cp_enable_ovpgate);
int platform_class_cp_get_present(unsigned int role, bool *present) { return 0; }
EXPORT_SYMBOL(platform_class_cp_get_present);
int platform_class_cp_enable_ovpgate_with_check(unsigned int role, int type_temp, bool en) { return 0; }
EXPORT_SYMBOL(platform_class_cp_enable_ovpgate_with_check);
int platform_class_cp_get_chip_vendor(unsigned int role, int *chip_vendor) { return 0; }
EXPORT_SYMBOL(platform_class_cp_get_chip_vendor);
int platform_class_cp_set_revchg(unsigned int role, bool en) { return 0; }
EXPORT_SYMBOL(platform_class_cp_set_revchg);
MODULE_LICENSE("GPL");
