// SPDX-License-Identifier: GPL-2.0
/* generated stub provider: only the export CRCs matter */
#include <linux/module.h>
#include <mca/strategy/strategy_class.h>

int mca_strategy_ops_register(unsigned int type, mca_strategy_func func, mca_strategy_get_status get_func, mca_strategy_set_config set_config, void *data) { return 0; }
EXPORT_SYMBOL(mca_strategy_ops_register);
int mca_strategy_func_get_status(int type, int status, void *value) { return 0; }
EXPORT_SYMBOL(mca_strategy_func_get_status);
int mca_strategy_func_process(unsigned int type, int event, int value) { return 0; }
EXPORT_SYMBOL(mca_strategy_func_process);
int mca_strategy_func_set_config(int type, int conifg, int value) { return 0; }
EXPORT_SYMBOL(mca_strategy_func_set_config);

MODULE_LICENSE("GPL");
