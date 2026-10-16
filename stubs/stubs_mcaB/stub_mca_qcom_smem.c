// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_qcom_smem (only the export CRCs matter). */
#include <linux/module.h>
#include <mca/qcom/mca_qcom_smem.h>

int get_smem_battery_info(int *is_zero_speed) { return 0; }
EXPORT_SYMBOL(get_smem_battery_info);
