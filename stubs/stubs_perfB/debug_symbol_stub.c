// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver): drivers/soc/qcom/debug_symbol.c exports this. */
#include <linux/module.h>

unsigned long debug_symbol_lookup_name(const char *name) { return 0; }
EXPORT_SYMBOL(debug_symbol_lookup_name);
MODULE_LICENSE("GPL");
