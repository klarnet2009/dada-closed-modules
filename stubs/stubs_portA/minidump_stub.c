// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the msm_minidump_* exports (real provider: stock minidump.ko, see ../minidump) so that dependants link; only for modpost/CRCs. */
#include <linux/module.h>
#include <soc/qcom/minidump.h>

int msm_minidump_add_region(const struct md_region *entry) { return 0; }
EXPORT_SYMBOL(msm_minidump_add_region);
int msm_minidump_remove_region(const struct md_region *entry) { return 0; }
EXPORT_SYMBOL(msm_minidump_remove_region);
MODULE_LICENSE("GPL");
