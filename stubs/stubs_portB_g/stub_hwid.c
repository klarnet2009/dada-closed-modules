// SPDX-License-Identifier: GPL-2.0
/* Stub provider of hwid.ko exports used by the portB modules. */
#include <linux/module.h>
#include "hwid.h"

uint32_t get_hw_country_version(void) { return 0; }
EXPORT_SYMBOL(get_hw_country_version);
