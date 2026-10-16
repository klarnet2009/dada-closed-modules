// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_charger_thermal.ko exports. */
#include <linux/module.h>
#include <mca/strategy/mca_charger_thermal.h>

int mca_get_wls_charger_thermal_remove(bool *wls_thermal_remove) { return 0; }
EXPORT_SYMBOL(mca_get_wls_charger_thermal_remove);
int mca_set_wls_charger_thermal_remove(bool wls_thermal_remove) { return 0; }
EXPORT_SYMBOL(mca_set_wls_charger_thermal_remove);
MODULE_LICENSE("GPL");
