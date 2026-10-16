// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of clk-qcom debugcc symbols imported by mist. */
#include <linux/module.h>
#include <soc/qcom/mist_ext.h>

int clk_debug_mc_cc_get(void *data, u64 *val)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(clk_debug_mc_cc_get);

u32 get_ddr_freq(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(get_ddr_freq);

MODULE_LICENSE("GPL");
