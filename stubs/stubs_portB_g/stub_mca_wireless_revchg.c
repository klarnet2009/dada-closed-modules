// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_wireless_revchg (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/strategy/strategy_wireless_class.h>

int mca_wireless_rev_get_fw_update(bool *fw_update) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_get_fw_update);
int mca_wireless_rev_set_usb_plugin(bool wls_sleep_usb_insert) { return 0; }
EXPORT_SYMBOL(mca_wireless_rev_set_usb_plugin);

