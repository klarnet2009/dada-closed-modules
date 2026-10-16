/* SPDX-License-Identifier: GPL-2.0 */
/* strategy_class.h - reconstructed API of mca_strategy_class.ko (prototypes from the XRING sources, CRC-verified against the stock exporter) */
#ifndef __MCA_STRATEGY_STRATEGY_CLASS_H__
#define __MCA_STRATEGY_STRATEGY_CLASS_H__

#include <linux/types.h>

enum strategy_func_type {
	STRATEGY_FUNC_TYPE_BUCK_CHARGE = 0,
	STRATEGY_FUNC_TYPE_QUICK_CHARGE = 1,
	STRATEGY_FUNC_TYPE_BASIC_WIRELESS = 2,
};

enum strategy_status_type {
	STRATEGY_STATUS_TYPE_ONLINE = 0,
	STRATEGY_STATUS_TYPE_QC_TYPE = 4,
	STRATEGY_STATUS_TYPE_POWER_MAX = 9,
};

int mca_strategy_func_get_status(int type, int status, void *value);
int mca_strategy_func_process(unsigned int type, int event, int value);

#endif /* __MCA_STRATEGY_STRATEGY_CLASS_H__ */
