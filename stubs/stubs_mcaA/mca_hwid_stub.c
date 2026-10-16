// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the mca_hwid.ko export. */
#include <linux/module.h>
#include <mca/common/mca_hwid.h>

const struct mca_hwid *mca_get_hwid_info(void) { return NULL; }
EXPORT_SYMBOL(mca_get_hwid_info);
MODULE_LICENSE("GPL");
