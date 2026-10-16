// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the minidump.ko exports used by the portD modules (modpost/CRC only). */
#include <linux/module.h>
#include <soc/qcom/minidump.h>

int msm_minidump_add_region(const struct md_region *entry) { return 0; }
EXPORT_SYMBOL(msm_minidump_add_region);
MODULE_LICENSE("GPL");
