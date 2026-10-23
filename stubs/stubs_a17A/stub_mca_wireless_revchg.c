// SPDX-License-Identifier: GPL-2.0
/* generated stub provider: only the export CRCs matter */
#include <linux/module.h>
#include <mca/strategy/mca_wireless_revchg.h>
#include <mca/strategy/strategy_wireless_class.h>

int mca_wireless_rev_update_fw_version(int cmd) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_update_fw_version);
int mca_wireless_rev_get_reverse_chg(bool *reverse_chg_en) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_get_reverse_chg);
int mca_wireless_rev_enable_reverse_charge(bool enable) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_enable_reverse_charge);
int mca_wireless_rev_set_user_reverse_chg(bool user_reverse_chg) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_set_user_reverse_chg);
int mca_wireless_rev_get_reverse_chg_state(int *state) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_get_reverse_chg_state);
int mca_wireless_rev_get_fw_update(bool *fw_update) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_get_fw_update);
int mca_wireless_rev_set_usb_plugin(bool wls_sleep_usb_insert) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_set_usb_plugin);
int mca_wireless_rev_get_rev_boost_default(int *rev_boost_default) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_get_rev_boost_default);

MODULE_LICENSE("GPL");
