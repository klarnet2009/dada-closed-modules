/* SPDX-License-Identifier: GPL-2.0 */
/* prototypes of the vendor symbols imported by the mist module (clk-qcom debugcc / dcvs providers) */
#ifndef _SOC_QCOM_MIST_EXT_H
#define _SOC_QCOM_MIST_EXT_H

#include <linux/types.h>

int clk_debug_mc_cc_get(void *data, u64 *val);
u32 get_ddr_freq(void);

#endif
