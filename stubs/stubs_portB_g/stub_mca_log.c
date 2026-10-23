// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_log (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/common/mca_log.h>

void __mca_log_err(const char *format, ...) { }
EXPORT_SYMBOL(__mca_log_err);
void __mca_log_info(const char *format, ...) { }
EXPORT_SYMBOL(__mca_log_info);
void __mca_log_debug(const char *format, ...) { }
EXPORT_SYMBOL(__mca_log_debug);
void mca_log_charge_log_register(enum mca_charge_log_id_ele type, struct mca_log_charge_log_ops *ops, void *data) { }
EXPORT_SYMBOL(mca_log_charge_log_register);

