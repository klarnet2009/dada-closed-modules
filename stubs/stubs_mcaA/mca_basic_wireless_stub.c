// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_basic_wireless.ko exports. */
#include <linux/module.h>
#include <mca/strategy/strategy_wireless_class.h>

int strategy_class_wireless_ops_get_wls_type(int *wls_type) { return 0; }
EXPORT_SYMBOL(strategy_class_wireless_ops_get_wls_type);
MODULE_LICENSE("GPL");
