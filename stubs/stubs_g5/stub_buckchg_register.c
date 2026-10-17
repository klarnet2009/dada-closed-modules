// SPDX-License-Identifier: GPL-2.0
/* stub provider of platform_class_buckchg_ops_register() with the REAL OS4 prototype: the genksyms CRC (0x129fc036) equals the stock OS4 export CRC,
 * which verifies the reconstructed 83-op struct platform_class_buckchg_ops (incl. set_cp_vendor_id) */
#include <linux/module.h>
#include <mca/platform/platform_buckchg_class.h>

int platform_class_buckchg_ops_register(unsigned int role, void *data, struct platform_class_buckchg_ops *ops) { return 0; }
EXPORT_SYMBOL(platform_class_buckchg_ops_register);
MODULE_LICENSE("GPL");
