// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the qcom-dcvs.ko export used by perfmgr (only the export CRC matters). */
#include <linux/module.h>

int boost_dcvs_freq(int freq, u32 hw_type) { return 0; }
EXPORT_SYMBOL(boost_dcvs_freq);
MODULE_LICENSE("GPL");
