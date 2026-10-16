// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of qcom-dcvs symbols imported by mist. */
#include <linux/module.h>
#include <soc/qcom/dcvs.h>

int qcom_dcvs_update_votes(const char *name, struct dcvs_freq *votes,
			   u32 update_mask, enum dcvs_path_type path)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(qcom_dcvs_update_votes);

MODULE_LICENSE("GPL");
