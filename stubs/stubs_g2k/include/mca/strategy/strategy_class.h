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

/* OS4 (HyperOS 4.0) numbering, from the module BTF of the stock mca_buckchg_jeita (enum mca_strategy_status): renumbered against OS3 */
enum strategy_status_type {
	STRATEGY_STATUS_TYPE_ONLINE = 0,
	STRATEGY_STATUS_TYPE_INIT_OK = 1,
	STRATEGY_STATUS_TYPE_CHARGING = 2,
	STRATEGY_STATUS_TYPE_QC_ENABLE = 3,
	STRATEGY_STATUS_TYPE_QC_TYPE = 4,
	STRATEGY_STATUS_TYPE_QC_IBAT_MAX = 5,
	STRATEGY_STATUS_TYPE_QC_START_FLAG = 6,
	STRATEGY_STATUS_TYPE_REV_TEST = 7,
	STRATEGY_STATUS_TYPE_WLS_MAGNET_LIMIT = 8,
	STRATEGY_STATUS_TYPE_POWER_MAX = 9,
	STRATEGY_STATUS_TYPE_ENABLE = 10,
	STRATEGY_STATUS_TYPE_MODE = 11,
	STRATEGY_STATUS_TYPE_VBUS = 12,
	STRATEGY_STATUS_TYPE_IBUS = 13,
	STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM = 14,
	STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM = 15,
	STRATEGY_STATUS_TYPE_JEITA_FFC_VTERM = 16,
	STRATEGY_STATUS_TYPE_JEITA_COLD_ZONE = 17,
	STRATEGY_STATUS_TYPE_JEITA_VFLOAT = 18,
	STRATEGY_STATUS_TYPE_JEITA_HIGH_TEMP_RANGE = 19,
	STRATEGY_STATUS_TYPE_CP_TO_PMIC = 20,
	STRATEGY_STATUS_TYPE_AUTH_PASS = 21,
	STRATEGY_STATUS_TYPE_BMD = 22,
	STRATEGY_STATUS_TYPE_DEVICE_POWER_MAX = 23,
	STRATEGY_STATUS_TYPE_FORCE_FULL_STATUS = 24,
	STRATEGY_STATUS_TYPE_ACTUAL_ITERM = 25,
	STRATEGY_STATUS_TYPE_ACTUAL_VTERM = 26,
	STRATEGY_STATUS_TYPE_SUPPORT_PPS_100W = 27,
	STRATEGY_STATUS_TYPE_JEITA_DYNAMIC_ITERM = 28,
	STRATEGY_STATUS_TYPE_JEITA_IS_DYNAMIC_ITERM_TEMP_RANGE = 29,
	STRATEGY_STATUS_TYPE_GET_VOLTAGE_MAX = 30,
	STRATEGY_STATUS_TYPE_GET_CURRENT_MAX = 31,
	STRATEGY_STATUS_TYPE_DROP_FLOAT_VOLT = 32,
	STRATEGY_STATUS_TYPE_MAX = 33,
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
