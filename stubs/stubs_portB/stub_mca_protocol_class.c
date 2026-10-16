// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_protocol_class (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/protocol/protocol_class.h>

int protocol_class_get_adapter_max_power(unsigned int protocol, unsigned int *max_power) { return 0; }
EXPORT_SYMBOL(protocol_class_get_adapter_max_power);
int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value) { return 0; }
EXPORT_SYMBOL(protocol_class_get_adapter_type);

