// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_protocol_class.ko exports. */
#include <linux/module.h>
#include <mca/protocol/protocol_class.h>

int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value) { return 0; }
EXPORT_SYMBOL(protocol_class_get_adapter_type);
int protocol_class_get_adapter_verified(unsigned int protocol, int *verified) { return 0; }
EXPORT_SYMBOL(protocol_class_get_adapter_verified);
int protocol_class_set_adapter_verified(unsigned int protocol, int verified) { return 0; }
EXPORT_SYMBOL(protocol_class_set_adapter_verified);
MODULE_LICENSE("GPL");
