/* SPDX-License-Identifier: GPL-2.0 */
/* strategy_class.h - reconstructed; enumerator values proven by the stock modules */
#ifndef __STRATEGY_CLASS_H__
#define __STRATEGY_CLASS_H__

enum strategy_func_type {
	STRATEGY_FUNC_TYPE_BUCK_CHARGE = 0,
	STRATEGY_FUNC_TYPE_QUICK_CHARGE = 1,
	STRATEGY_FUNC_TYPE_BASIC_WIRELESS = 2,
	STRATEGY_FUNC_TYPE_QUICK_WIRELESS = 3,
	STRATEGY_FUNC_TYPE_REV_WIRELESS = 4,
	STRATEGY_FUNC_TYPE_THERMAL = 5,
	STRATEGY_FUNC_TYPE_FG = 6,
	STRATEGY_FUNC_TYPE_JEITA = 7,
	STRATEGY_FUNC_TYPE_SMARTCHG = 8,
	STRATEGY_FUNC_TYPE_BMD = 9,
	STRATEGY_FUNC_TYPE_CONNECTOR_ANTIBURN = 10,
	STRATEGY_FUNC_TYPE_MAX,
};

enum strategy_status_type {
	STRATEGY_STATUS_TYPE_ONLINE = 0,
	STRATEGY_STATUS_TYPE_INIT_OK = 1,
	STRATEGY_STATUS_TYPE_QC_TYPE = 4,
	STRATEGY_STATUS_TYPE_POWER_MAX = 9,
	STRATEGY_STATUS_TYPE_ENABLE = 0xa,
	STRATEGY_STATUS_TYPE_MODE = 0xb,
	STRATEGY_STATUS_TYPE_VBUS = 0xc,
	STRATEGY_STATUS_TYPE_IBUS = 0xd,
};

enum strategy_config_type {
	STRATEGY_CONFIG_INPUT_CURRENT_LIMIT = 0,
};

typedef int (*mca_strategy_func)(int event, int value, void *data);
typedef int (*mca_strategy_get_status)(int status, void *value, void *data);
typedef int (*mca_strategy_set_config)(int config, int value, void *data);

int mca_strategy_ops_register(unsigned int type, mca_strategy_func func,
	mca_strategy_get_status get_func, mca_strategy_set_config set_config, void *data);
int mca_strategy_func_get_status(int type, int status, void *value);
int mca_strategy_func_process(unsigned int type, int event, int value);
int mca_strategy_func_set_config(int type, int conifg, int value);

#endif
