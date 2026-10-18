/* SPDX-License-Identifier: GPL-2.0 */
/* mca_charger_thermal.h - reconstructed API of mca_charger_thermal.ko (prototypes from the XRING sources, CRC-verified against the stock exporter) */
#ifndef __MCA_STRATEGY_MCA_CHARGER_THERMAL_H__
#define __MCA_STRATEGY_MCA_CHARGER_THERMAL_H__

#include <linux/types.h>

int mca_get_wls_charger_thermal_remove(bool *wls_thermal_remove);
int mca_set_wls_charger_thermal_remove(bool wls_thermal_remove);

#endif /* __MCA_STRATEGY_MCA_CHARGER_THERMAL_H__ */
