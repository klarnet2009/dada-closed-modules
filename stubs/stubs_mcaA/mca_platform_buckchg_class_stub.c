// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_platform_buckchg_class.ko exports. */
#include <linux/module.h>
#include <mca/platform/platform_buckchg_class.h>

int platform_class_buckchg_ops_register(unsigned int role, void *data, struct platform_class_buckchg_ops *ops) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_register);
int platform_class_buckchg_ops_get_chg_type(unsigned int role, int *chg_type) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_chg_type);
int platform_class_buckchg_ops_is_support_cid(unsigned int role, bool *en) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_is_support_cid);
int platform_class_buckchg_ops_get_pack_ibat(unsigned int role, int *pibat) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_pack_ibat);
int platform_class_buckchg_ops_get_ship_mode(unsigned int role, bool *en) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_ship_mode);
int platform_class_buckchg_ops_set_ship_mode(unsigned int role, bool en) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_set_ship_mode);
MODULE_LICENSE("GPL");
