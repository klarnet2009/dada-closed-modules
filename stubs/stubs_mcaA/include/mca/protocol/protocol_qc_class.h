/* SPDX-License-Identifier: GPL-2.0 */
/* protocol_qc_class.h - QC protocol class (provider: mca_protocol_qc_class.ko), reconstructed from the stock BTF */
#ifndef __PROTOCOL_QC_CLASS_H__
#define __PROTOCOL_QC_CLASS_H__

#include <linux/types.h>

struct protocol_class_qc_ops {
	int (*protocol_qc_get_qc_type)(int *type, void *data);
	int (*protocol_qc_set_volt)(void *data, int volt);
	int (*protocol_qc_set_volt_cmd)(void *data, int cmd);
	int (*protocol_qc3_check_class_type)(int *type, void *data);
};

int protocol_class_qc_register_ops(unsigned int role, struct protocol_class_qc_ops *ops, void *data);

#endif /* __PROTOCOL_QC_CLASS_H__ */
