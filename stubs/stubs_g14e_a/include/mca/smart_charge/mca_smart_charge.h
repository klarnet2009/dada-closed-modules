/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MCA_SMART_CHARGE_H__
#define __MCA_SMART_CHARGE_H__

int mca_smartchg_get_limit_soc(void);
int mca_smartchg_is_extreme_cold_enabled(void);
int mca_smartchg_set_batt_emerg_soc_limit(int limit);	/* GLOBAL OS4.0.0.7 only (G14e); the stock returns the vote result / garbage, callers ignore it */

#endif
