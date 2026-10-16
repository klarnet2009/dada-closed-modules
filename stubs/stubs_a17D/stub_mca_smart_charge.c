// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_smart_charge.ko exports used by a17D modules (only the export CRCs matter). */
#include <linux/module.h>

void mca_smartchg_set_scene(int scene) { }
EXPORT_SYMBOL(mca_smartchg_set_scene);
MODULE_LICENSE("GPL");
