// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of qcom-scm symbols imported by mi_power. */
#include <linux/module.h>
#include <linux/firmware/qcom/qcom_scm.h>

void qcom_scm_set_download_mode(enum qcom_download_mode mode)
{
}
EXPORT_SYMBOL_GPL(qcom_scm_set_download_mode);

MODULE_LICENSE("GPL");
