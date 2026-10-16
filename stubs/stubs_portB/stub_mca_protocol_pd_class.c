// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_protocol_pd_class (only the export CRCs matter). */
#include <linux/module.h>
#include <mca/protocol/protocol_pd_class.h>

int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_get_cid_status);
