// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of the qcom socinfo symbols imported by hwid. */
#include <linux/module.h>
#include <soc/qcom/socinfo.h>

uint32_t socinfo_get_id(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(socinfo_get_id);

MODULE_LICENSE("GPL");
