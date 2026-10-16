// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the mca_platform_bc12_class.ko export. */
#include <linux/module.h>
#include <mca/platform/platform_bc12_class.h>

int platform_bc12_class_ops_register(unsigned int role, struct platform_bc12_class_ops *ops, void *data) { return 0; }
EXPORT_SYMBOL(platform_bc12_class_ops_register);
MODULE_LICENSE("GPL");
