// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_platform_wireless_class (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/platform/platform_wireless_class.h>

int platform_class_wireless_set_enable_mode(unsigned int role, bool enable) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_enable_mode);

