#include <linux/module.h>
#include "mca_log.h"
void __mca_log_err(const char *fmt, ...) { }
EXPORT_SYMBOL(__mca_log_err);
MODULE_LICENSE("GPL");
