/* SPDX-License-Identifier: GPL-2.0 */
/* mca_wireless_revchg.h - reconstructed API of mca_wireless_revchg.ko (prototypes from the XRING sources, CRC-checked against the OS4 stock exporter) */
#ifndef __MCA_STRATEGY_MCA_WIRELESS_REVCHG_H__
#define __MCA_STRATEGY_MCA_WIRELESS_REVCHG_H__

#include <linux/types.h>
#include <linux/notifier.h>

int mca_wireless_rev_get_rev_boost_default(int *rev_boost_default);
int mca_wireless_rev_get_user_reverse_chg(bool *user_reverse_chg);
int mca_wireless_rev_set_user_reverse_chg(bool user_reverse_chg);
int mca_wireless_rev_get_reverse_chg(bool *reverse_chg_en);
int mca_wireless_rev_set_reverse_chg(bool reverse_chg_en);
int mca_wireless_rev_set_usb_plugin(bool wls_sleep_usb_insert);
int mca_wireless_rev_get_fw_update(bool *fw_update);
int mca_wireless_rev_get_reverse_chg_state(int *state);
int mca_wireless_rev_set_firmware_state(int fw_update_state);
int mca_wireless_rev_get_firmware_state(int *state);
int mca_wireless_rev_update_fw_version(int cmd);
int mca_wireless_rev_set_wired_chg_ok(bool ok);
int mca_wireless_rev_enable_reverse_charge(bool enable);
int pen_charge_state_notifier_register_client(struct notifier_block *nb);
int pen_charge_state_notifier_unregister_client(struct notifier_block *nb);

#endif
