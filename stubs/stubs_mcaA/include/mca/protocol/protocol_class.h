/* SPDX-License-Identifier: GPL-2.0 */
/* protocol_class.h - reconstructed API of mca_protocol_class.ko (prototypes from the XRING sources, CRC-verified against the stock exporter) */
#ifndef __MCA_PROTOCOL_PROTOCOL_CLASS_H__
#define __MCA_PROTOCOL_PROTOCOL_CLASS_H__

#include <linux/types.h>

enum adapter_protocol_type {
	ADAPTER_PROTOCOL_PD = 0,
	ADAPTER_PROTOCOL_BC12 = 5,
};

int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value);
int protocol_class_get_adapter_verified(unsigned int protocol, int *verified);
int protocol_class_set_adapter_verified(unsigned int protocol, int verified);

#endif /* __MCA_PROTOCOL_PROTOCOL_CLASS_H__ */
