// SPDX-License-Identifier: GPL-2.0
/* Stub provider of machine_dlkm exports (resolves modpost imports); check/set/get_smartpa_type have the stock CRCs. */
#include <linux/module.h>
#include <linux/types.h>

bool check_smartpa_type(const char *name) { return true; }
EXPORT_SYMBOL(check_smartpa_type);
int set_smartpa_type(const char *name, unsigned int type) { return 0; }
EXPORT_SYMBOL(set_smartpa_type);
int get_smartpa_type(void) { return 0; }
EXPORT_SYMBOL(get_smartpa_type);
/* prototype unrecovered (CRC 0x77aedcd7 not reproduced) */
void set_smartpa_codec(const char *codec_name, const char *dai_name) { }
EXPORT_SYMBOL(set_smartpa_codec);
MODULE_LICENSE("GPL");
