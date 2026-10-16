// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of debug-regulator symbols imported by mi_power. */
#include <linux/module.h>

void regulator_debug_print_enable_regulators(void)
{
}
EXPORT_SYMBOL(regulator_debug_print_enable_regulators);

MODULE_LICENSE("GPL");
