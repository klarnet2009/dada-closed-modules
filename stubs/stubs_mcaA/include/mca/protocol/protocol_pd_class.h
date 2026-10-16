/* SPDX-License-Identifier: GPL-2.0 */
/* protocol_pd_class.h - reconstructed API of mca_protocol_pd_class.ko (prototypes from the XRING sources, CRC-verified against the stock exporter) */
#ifndef __MCA_PROTOCOL_PROTOCOL_PD_CLASS_H__
#define __MCA_PROTOCOL_PROTOCOL_PD_CLASS_H__

#include <linux/types.h>

enum typec_port_num {
	TYPEC_PORT_0 = 0,
};

int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status);
int protocol_class_pd_get_cc_toggle(unsigned int port_num, bool *en);
int protocol_class_pd_set_cc_toggle(unsigned int port_num, bool en);
int protocol_class_pd_get_has_dp(unsigned int port_num, bool *has_dp);
int protocol_class_pd_get_pd_active(unsigned int port_num, int *pd_active);
int protocol_class_pd_set_pd_active(unsigned int port_num, int pd_active);

#endif /* __MCA_PROTOCOL_PROTOCOL_PD_CLASS_H__ */
