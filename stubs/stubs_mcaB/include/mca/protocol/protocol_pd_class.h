/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MCA_PROTOCOL_PROTOCOL_PD_CLASS_H__
#define __MCA_PROTOCOL_PROTOCOL_PD_CLASS_H__

#include <linux/types.h>

int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status);

#endif
