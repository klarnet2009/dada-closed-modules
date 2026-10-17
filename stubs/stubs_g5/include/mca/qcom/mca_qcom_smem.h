/* SPDX-License-Identifier: GPL-2.0 */
/* mca_qcom_smem.h - battery info shared by the bootloader through SMEM (provider: mca_qcom_smem.ko) */
#ifndef __MCA_QCOM_SMEM_H__
#define __MCA_QCOM_SMEM_H__

#include <linux/types.h>

int get_smem_battery_info(int *is_zero_speed);

#endif
