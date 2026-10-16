// SPDX-License-Identifier: GPL-2.0
/* Stub provider of qcom_ipc_logging exports (resolves modpost imports with the stock CRCs). */
#include <linux/module.h>
#include <linux/ipc_logging.h>

void *ipc_log_context_create(int max_num_pages, const char *modname, uint32_t feature_version) { return NULL; }
EXPORT_SYMBOL(ipc_log_context_create);
int ipc_log_string(void *ilctxt, const char *fmt, ...) { return 0; }
EXPORT_SYMBOL(ipc_log_string);
int ipc_log_context_destroy(void *ctxt) { return 0; }
EXPORT_SYMBOL(ipc_log_context_destroy);
MODULE_LICENSE("GPL");
