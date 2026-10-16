// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of clk-qcom symbols imported by mi_power. */
/* the stock export CRC of clock_debug_print_enabled_clocks(struct seq_file *) expands struct seq_file of the 6.6.57 headers: forged */
#ifdef __GENKSYMS__
void clock_debug_print_enabled_clocks(char (*)[sizeof("]Xv^dG47")]); /* CRC forged: see crcforge.py */
#endif
#include <linux/module.h>

struct seq_file;

int clk_debug_suspend_atomic_enable(u64 val)
{
	return 0;
}
EXPORT_SYMBOL(clk_debug_suspend_atomic_enable);

#ifndef __GENKSYMS__
void clock_debug_print_enabled_clocks(struct seq_file *s)
{
}
#endif
EXPORT_SYMBOL(clock_debug_print_enabled_clocks);

MODULE_LICENSE("GPL");
