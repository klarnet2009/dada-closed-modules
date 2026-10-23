/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __STRATEGY_WIRELESS_CLASS_H__
#define __STRATEGY_WIRELESS_CLASS_H__

#include <linux/types.h>

enum firmware_update_state {
	FIRMWARE_NO_UPDATE = 0,
	FIRMWARE_UPDATING,
	FIRMWARE_UPDATE_ERROR,
	FIRMWARE_UPDATE_FINISH,
};

enum rev_boost_src {
	PMIC_REV_BOOST = 0,
	PMIC_HBOOST,
	EXTERNAL_BOOST,
	CHARGER_ADAPTER,
	BOOST_SRC_MAX,
};

typedef enum {
	REV_EN_BOOST = 0,
} EN_SRC;

int mca_wireless_rev_get_fw_update(bool *fw_update);
int mca_wireless_rev_set_usb_plugin(bool wls_sleep_usb_insert);

#endif
