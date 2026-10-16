// SPDX-License-Identifier: GPL-2.0
/* Stub provider of qcom_va_minidump exports (resolves modpost imports with the stock CRCs). */
#include <linux/module.h>
#include <soc/qcom/minidump.h>

int qcom_va_md_register(const char *name, struct notifier_block *nb) { return 0; }
EXPORT_SYMBOL(qcom_va_md_register);
int qcom_va_md_add_region(struct va_md_entry *entry) { return 0; }
EXPORT_SYMBOL(qcom_va_md_add_region);
MODULE_LICENSE("GPL");
