// SPDX-License-Identifier: GPL-2.0
/* stub provider (mca_strategy_class, OS4 addition) with the REAL prototype: genksyms CRC 0xcb972af3 equals the stock OS4 export CRC */
#include <linux/module.h>

bool mca_strategy_is_support_pps_100w(void) { return 0; }
EXPORT_SYMBOL(mca_strategy_is_support_pps_100w);
MODULE_LICENSE("GPL");
