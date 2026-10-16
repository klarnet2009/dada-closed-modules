/* SPDX-License-Identifier: GPL-2.0 */
/* mca_wireless_revchg.h - reconstructed API of mca_wireless_revchg.ko (prototypes from the XRING sources, CRC-verified against the stock exporter) */
#ifndef __MCA_STRATEGY_MCA_WIRELESS_REVCHG_H__
#define __MCA_STRATEGY_MCA_WIRELESS_REVCHG_H__

#include <linux/types.h>

int mca_wireless_rev_update_fw_version(int cmd);
int mca_wireless_rev_get_reverse_chg(bool *reverse_chg_en);
int mca_wireless_rev_enable_reverse_charge(bool enable);
int mca_wireless_rev_set_user_reverse_chg(bool user_reverse_chg);
int mca_wireless_rev_get_reverse_chg_state(int *state);

#endif /* __MCA_STRATEGY_MCA_WIRELESS_REVCHG_H__ */
