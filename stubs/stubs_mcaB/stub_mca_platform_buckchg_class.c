// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_platform_buckchg_class (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/platform/platform_buckchg_class.h>

int platform_class_buckchg_ops_is_init_ok(unsigned int role) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_is_init_ok);
int platform_class_buckchg_ops_set_too_hot_limit(unsigned int role, int enable) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_set_too_hot_limit);
int platform_class_buckchg_ops_get_bus_volt(unsigned int role, int *bus_volt) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_bus_volt);
int platform_class_buckchg_ops_get_bus_curr(unsigned int role, int *bus_curr) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_bus_curr);
int platform_class_buckchg_ops_get_usb_sns_volt(unsigned int role, int *bus_volt) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_usb_sns_volt);
int platform_class_buckchg_ops_get_chg_status(unsigned int role, int *chg_status) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_chg_status);
int platform_class_buckchg_ops_get_pack_vbat(unsigned int role, int *pvbat) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_pack_vbat);
int platform_class_buckchg_ops_get_pack_ibat(unsigned int role, int *pibat) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_pack_ibat);
int platform_class_buckchg_ops_get_pack_tbat(unsigned int role, int *ptbat) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_get_pack_tbat);

