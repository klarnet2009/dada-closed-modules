// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the mca_log.ko exports (exists only so modpost resolves/CRCs the imports of the module under test). */
#include <linux/module.h>
#include <mca/common/mca_log.h>

void __mca_log_err(const char *format, ...) { }
EXPORT_SYMBOL(__mca_log_err);
void __mca_log_info(const char *format, ...) { }
EXPORT_SYMBOL(__mca_log_info);
void __mca_log_debug(const char *format, ...) { }
EXPORT_SYMBOL(__mca_log_debug);
int mca_log_get_charge_boot_mode(void) { return 0; }
EXPORT_SYMBOL(mca_log_get_charge_boot_mode);
void mca_log_charge_log_register(enum mca_charge_log_id_ele type,
	struct mca_log_charge_log_ops *ops, void *data) { }
EXPORT_SYMBOL(mca_log_charge_log_register);
MODULE_LICENSE("GPL");
