// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of qcom-pmu-lib symbols imported by mist. */
#include <linux/module.h>
#include <soc/qcom/pmu_lib.h>

int qcom_pmu_read_all(int cpu, struct qcom_pmu_data *data)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(qcom_pmu_read_all);

int qcom_pmu_read_all_local(struct qcom_pmu_data *data)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(qcom_pmu_read_all_local);

MODULE_LICENSE("GPL");
