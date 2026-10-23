/* SPDX-License-Identifier: GPL-2.0 */
/* protocol_qc_class.h - reconstructed public API of mca_protocol_qc_class.ko (ops layout from the stock BTF, names from the sources) */
#ifndef __MCA_PROTOCOL_PROTOCOL_QC_CLASS_H__
#define __MCA_PROTOCOL_PROTOCOL_QC_CLASS_H__

#include <linux/types.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>	/* TYPEC_PORT_* */

struct protocol_class_qc_ops {
	int (*protocol_qc_get_qc_type)(int *type, void *data);
	int (*protocol_qc_set_volt)(void *data, int volt);
	int (*protocol_qc_set_volt_cmd)(void *data, int hvdcp_cmd);
	int (*protocol_qc3_check_class_type)(int *type, void *data);
};

int protocol_class_qc3_check_class_type(unsigned int port_num, int *type);
int protocol_class_qc_get_qc_type(unsigned int port_num, int *type);
int protocol_class_qc_set_volt(unsigned int port_num, int volt);
int protocol_class_qc_set_volt_cmd(unsigned int port_num, int hvdcp_cmd);
int protocol_class_qc_register_ops(unsigned int port_num, struct protocol_class_qc_ops *ops, void *data);

#endif
