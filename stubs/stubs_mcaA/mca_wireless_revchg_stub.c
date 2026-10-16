// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_wireless_revchg.ko exports. */
#include <linux/module.h>
#include <mca/strategy/mca_wireless_revchg.h>

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
MODULE_LICENSE("GPL");
