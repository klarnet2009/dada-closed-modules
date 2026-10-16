// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_hw (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/hardware/hw_connector_antiburn.h>
#include <mca/hardware/hw_lpd_detect.h>

int connector_antiburn_is_triggered(void) { return 0; }
EXPORT_SYMBOL(connector_antiburn_is_triggered);
int lpd_is_charging_limit(void) { return 0; }
EXPORT_SYMBOL(lpd_is_charging_limit);

