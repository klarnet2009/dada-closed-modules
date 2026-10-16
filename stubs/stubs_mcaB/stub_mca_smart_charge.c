// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_smart_charge (only the export CRCs matter). */
#include <linux/module.h>
#include <mca/smart_charge/mca_smart_charge.h>

int mca_smartchg_get_limit_soc(void) { return 0; }
EXPORT_SYMBOL(mca_smartchg_get_limit_soc);
int mca_smartchg_is_extreme_cold_enabled(void) { return 0; }
EXPORT_SYMBOL(mca_smartchg_is_extreme_cold_enabled);
