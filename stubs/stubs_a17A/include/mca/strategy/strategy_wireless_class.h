/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __STRATEGY_WIRELESS_CLASS_H__
#define __STRATEGY_WIRELESS_CLASS_H__

#include <linux/types.h>

int mca_wireless_rev_get_fw_update(bool *fw_update);
int mca_wireless_rev_set_usb_plugin(bool wls_sleep_usb_insert);
int mca_wireless_rev_get_rev_boost_default(int *rev_boost_default);
int mca_wireless_rev_get_reverse_chg(bool *reverse_chg_en);

#endif
