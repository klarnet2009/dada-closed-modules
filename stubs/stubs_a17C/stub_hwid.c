// SPDX-License-Identifier: GPL-2.0
/* Stub provider of hwid.ko exports used by the a17C modules (only exists so modpost resolves the imports). */
#include <linux/module.h>
#include "hwid.h"

const char *product_name_get(void) { return "xuanyuan"; }
EXPORT_SYMBOL(product_name_get);
MODULE_LICENSE("GPL");
