// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_strategy_class.ko exports. */
#include <linux/module.h>
#include <mca/strategy/strategy_class.h>

int mca_strategy_func_get_status(int type, int status, void *value) { return 0; }
EXPORT_SYMBOL(mca_strategy_func_get_status);
int mca_strategy_func_process(unsigned int type, int event, int value) { return 0; }
EXPORT_SYMBOL(mca_strategy_func_process);
MODULE_LICENSE("GPL");
