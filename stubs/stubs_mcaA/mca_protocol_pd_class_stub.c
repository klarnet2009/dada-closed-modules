// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_protocol_pd_class.ko exports. */
#include <linux/module.h>
#include <mca/protocol/protocol_pd_class.h>

int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_cid_status);
int protocol_class_pd_get_cc_toggle(unsigned int port_num, bool *en) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_cc_toggle);
int protocol_class_pd_set_cc_toggle(unsigned int port_num, bool en) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_set_cc_toggle);
int protocol_class_pd_get_has_dp(unsigned int port_num, bool *has_dp) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_has_dp);
int protocol_class_pd_get_pd_active(unsigned int port_num, int *pd_active) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_pd_active);
int protocol_class_pd_set_pd_active(unsigned int port_num, int pd_active) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_set_pd_active);
MODULE_LICENSE("GPL");
