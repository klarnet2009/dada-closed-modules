// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_platform_loadsw_class (only the export CRCs matter). */
#include <linux/module.h>
#include <mca/platform/platform_loadsw_class.h>

int platform_class_loadsw_set_lowpower_mode(unsigned int role, bool mode) { return 0; }
EXPORT_SYMBOL(platform_class_loadsw_set_lowpower_mode);
