// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the mca_protocol_qc_class.ko export. */
#include <linux/module.h>
#include <mca/protocol/protocol_qc_class.h>

int protocol_class_qc_register_ops(unsigned int role, struct protocol_class_qc_ops *ops, void *data) { return 0; }
EXPORT_SYMBOL(protocol_class_qc_register_ops);
MODULE_LICENSE("GPL");
